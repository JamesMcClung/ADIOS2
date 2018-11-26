/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * StagingWriter.cpp
 *
 *  Created on: Nov 1, 2018
 *      Author: Jason Wang
 */

#include "StagingWriter.h"
#include "StagingWriter.tcc"

#include "adios2/helper/adiosFunctions.h"
#include "adios2/toolkit/transport/file/FileFStream.h"

#include <iostream>

#include <zmq.h>

namespace adios2
{
namespace core
{
namespace engine
{

StagingWriter::StagingWriter(IO &io, const std::string &name, const Mode mode,
                             MPI_Comm mpiComm)
: Engine("StagingWriter", io, name, mode, mpiComm),
  m_DataManSerializer(helper::IsRowMajor(io.m_HostLanguage),
                      helper::IsLittleEndian()),
  m_MetadataTransport(mpiComm, m_DebugMode),
  m_DataTransport(mpiComm, m_DebugMode)
{
    m_EndMessage = " in call to StagingWriter " + m_Name + " Open\n";
    MPI_Comm_rank(mpiComm, &m_MpiRank);
    MPI_Comm_size(mpiComm, &m_MpiSize);
    Init();
    if (m_Verbosity == 5)
    {
        std::cout << "Staging Writer " << m_MpiRank << " Open(" << m_Name
                  << ")." << std::endl;
    }
}

StepStatus StagingWriter::BeginStep(StepMode mode, const float timeoutSeconds)
{
    m_CurrentStep++; // 0 is the first step

    m_DataManSerializer.New(1024);

    if (m_Verbosity == 5)
    {
        std::cout << "Staging Writer " << m_MpiRank
                  << "   BeginStep() new step " << m_CurrentStep << "\n";
    }
    return StepStatus::OK;
}

size_t StagingWriter::CurrentStep() const
{
    if (m_Verbosity == 5)
    {
        std::cout << "Staging Writer " << m_MpiRank
                  << "   CurrentStep() returns " << m_CurrentStep << "\n";
    }
    return m_CurrentStep;
}

/* PutDeferred = PutSync, so nothing to be done in PerformPuts */
void StagingWriter::PerformPuts() {}

void StagingWriter::EndStep()
{
    auto aggMetadata = m_DataManSerializer.GetAggregatedMetadata(m_MPIComm);
    m_MetadataTransport.Write(aggMetadata, 0);

    m_DataManSerializer.PutPack(m_DataManSerializer.GetLocalPack());

    if (m_Verbosity == 5)
    {
        std::cout << "Staging Writer " << m_MpiRank << "   EndStep()\n";
    }
}

void StagingWriter::Flush(const int transportIndex)
{
    if (m_Verbosity == 5)
    {
        std::cout << "Staging Writer " << m_MpiRank << "   Flush()\n";
    }
}

// PRIVATE

#define declare_type(T)                                                        \
    void StagingWriter::DoPutSync(Variable<T> &variable, const T *data)        \
    {                                                                          \
        PutSyncCommon(variable, data);                                         \
    }                                                                          \
    void StagingWriter::DoPutDeferred(Variable<T> &variable, const T *data)    \
    {                                                                          \
        PutDeferredCommon(variable, data);                                     \
    }
ADIOS2_FOREACH_TYPE_1ARG(declare_type)
#undef declare_type

void StagingWriter::Init()
{
    InitParameters();
    Handshake();
    InitTransports();
}

void StagingWriter::InitParameters()
{
    for (const auto &pair : m_IO.m_Parameters)
    {
        std::string key(pair.first);
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        std::string value(pair.second);
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);

        if (key == "verbose")
        {
            m_Verbosity = std::stoi(value);
            if (m_DebugMode)
            {
                if (m_Verbosity < 0 || m_Verbosity > 5)
                    throw std::invalid_argument(
                        "ERROR: Method verbose argument must be an "
                        "integer in the range [0,5], in call to "
                        "Open or Engine constructor\n");
            }
        }
    }
}

void StagingWriter::InitTransports()
{

    if (m_MpiRank == 0)
    {
        Params params;
        params["IPAddress"] = m_IP;
        params["Port"] = m_MetadataPort;
        params["Library"] = "zmq";
        params["Name"] = m_Name;
        std::vector<Params> paramsVec;
        paramsVec.emplace_back(params);
        m_MetadataTransport.OpenTransports(paramsVec, Mode::Write, "subscribe",
                                           true);
    }
}

void StagingWriter::Handshake()
{
    auto ips = helper::AvailableIpAddresses();
    if (ips.size() < 1)
    {
        throw(std::runtime_error("No network interface available"));
        return;
    }
    int useIP = 0;
    transport::FileFStream ipstream(m_MPIComm, m_DebugMode);
    ipstream.Open(".StagingHandshake", Mode::Write);
    ipstream.Write(ips[0].data(), ips[0].size());
    ipstream.Close();

    m_IP = ips[0];
    int port = 12306 + m_MpiRank;
    m_MetadataPort = std::to_string(port);
    port = 12306 + m_MpiSize + m_MpiRank;
    m_DataPort = std::to_string(port);
}

void StagingWriter::DoClose(const int transportIndex)
{
    if (m_Verbosity == 5)
    {
        std::cout << "Staging Writer " << m_MpiRank << " Close(" << m_Name
                  << ")\n";
    }
}

} // end namespace engine
} // end namespace core
} // end namespace adios2
