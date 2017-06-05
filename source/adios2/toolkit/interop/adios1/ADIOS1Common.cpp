/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * ADIOS1Common.cpp
 *
 *  Created on: Jun 1, 2017
 *      Author: Norbert Podhorszki pnorbert@ornl.gov
 *              William F Godoy godoywf@ornl.gov
 */

#include "ADIOS1Common.h"
#include "ADIOS1Common.tcc"

#include "adios2/ADIOSMPI.h"
#include "adios2/helper/adiosFunctions.h" //OpenModeToString, GetType

extern int adios_verbose_level;
extern int adios_errno;

namespace adios
{
namespace interop
{

ADIOS1Common::ADIOS1Common(const std::string &groupName,
                           const std::string &fileName, const OpenMode openMode,
                           MPI_Comm mpiComm, const bool debugMode)
: m_GroupName(groupName), m_FileName(fileName),
  m_OpenModeString(OpenModeToString(openMode)), m_MPIComm(mpiComm),
  m_DebugMode(debugMode)
{
    Init();
}

ADIOS1Common::~ADIOS1Common() { Close(); }

void ADIOS1Common::Advance() { Close(); }

void ADIOS1Common::Close()
{
    if (m_IsFileOpen)
    {
        adios_close(m_ADIOSFile);
        m_IsFileOpen = false;
    }
}

// PRIVATE
void ADIOS1Common::Init()
{
    if (!m_IsInitialized)
    {
        adios_init_noxml(m_MPIComm);
        m_IsInitialized = true;
    }
    adios_declare_group(&m_ADIOSGroup, m_GroupName.c_str(), "",
                        adios_stat_default);
}

void ADIOS1Common::InitParameters(const Params &parameters)
{
    auto itMaxBufferSize = parameters.find("max_size_MB");
    if (itMaxBufferSize != parameters.end())
    {
        adios_set_max_buffer_size(std::stoul(itMaxBufferSize->second));
    }

    auto itVerbosity = parameters.find("verbose");
    if (itVerbosity != parameters.end())
    {
        int verbosity = std::stoi(itVerbosity->second);
        if (m_DebugMode == true)
        {
            if (verbosity < 0 || verbosity > 5)
                throw std::invalid_argument(
                    "ERROR: Method verbose argument must be an "
                    "integer in the range [0,5], in call to "
                    "Open or Engine constructor\n");
        }
        adios_verbose_level = verbosity;
    }
}

void ADIOS1Common::InitTransports(
    const std::vector<Params> &transportsParameters)
{
    for (const auto &parameters : transportsParameters)
    {
        auto itTransport = parameters.find("transport");

        if (itTransport->second == "file" || itTransport->second == "File")
        {
            auto itLibrary = parameters.find("library");
            if (itLibrary == parameters.end() ||
                itLibrary->second == "POSIX") // use default POSIX
            {
                adios_select_method(m_ADIOSGroup, "POSIX", "", "");
            }
            else if (itLibrary->second == "MPI_File" ||
                     itLibrary->second == "MPI-IO" ||
                     itLibrary->second == "MPI")
            {
                adios_select_method(m_ADIOSGroup, "MPI", "", "");
            }
            else
            {
                if (m_DebugMode == true)
                    throw std::invalid_argument(
                        "ERROR: file transport library " + itLibrary->second +
                        " for file " + m_FileName +
                        " not supported, in ADIOS1 " + ", in call to Open\n");
            }
        }
        else
        {
            if (m_DebugMode == true)
                throw std::invalid_argument("ERROR: invalid transport " +
                                            itTransport->second +
                                            ", only transports of type "
                                            "file supported in ADIOS1, in call "
                                            "to Open\n");
        }
    }
}

bool ADIOS1Common::Open()
{
    adios_open(&m_ADIOSFile, m_GroupName.c_str(), m_FileName.c_str(),
               m_OpenModeString.c_str(), m_MPIComm);
    if (adios_errno == err_no_error)
    {
        m_IsFileOpen = true;
    }
    return m_IsFileOpen;
}

bool ADIOS1Common::ReOpenAsNeeded()
{
    if (!m_IsFileOpen)
    {
        // Re-open in APPEND mode
        adios_open(&m_ADIOSFile, m_GroupName.c_str(), m_FileName.c_str(), "a",
                   m_MPIComm);
        if (adios_errno == err_no_error)
        {
            m_IsFileOpen = true;
            adios_delete_vardefs(m_ADIOSGroup);
        }
    }
    return m_IsFileOpen;
}

void ADIOS1Common::DefineVariable(const std::string &name,
                                  const ShapeID shapeID,
                                  enum ADIOS_DATATYPES vartype,
                                  const std::string ldims,
                                  const std::string gdims,
                                  const std::string offsets)
{
    switch (shapeID)
    {
    case ShapeID::GlobalValue:
        adios_define_var(m_ADIOSGroup, name.c_str(), "", vartype, "", "", "");
        break;
    case ShapeID::LocalValue:
        adios_define_var(m_ADIOSGroup, name.c_str(), "", vartype, "", "", "");
        adios_define_attribute(m_ADIOSGroup, "ReadAsArray", name.c_str(),
                               adios_byte, "1", nullptr);
        break;
    case ShapeID::GlobalArray:
    case ShapeID::LocalArray:
    case ShapeID::JoinedArray:
        adios_define_var(m_ADIOSGroup, name.c_str(), "", vartype, ldims.c_str(),
                         gdims.c_str(), offsets.c_str());
        break;
    }
}

std::string ADIOS1Common::DimsToCSVLocalAware(const Dims &dims)
{
    std::string dimsCSV;
    bool localVariable = false;

    for (const auto dim : dims)
    {
        dimsCSV += std::to_string(dim) + ",";
        if (dim == JoinedDim || dim == IrregularDim)
        {
            localVariable = true;
        }
    }

    if (dimsCSV.empty() == false)
    {
        dimsCSV.pop_back();
    }

    return dimsCSV;
}

// Explicit declaration of the public template methods
#define declare_template_instantiation(T)                                      \
    template void ADIOS1Common::WriteVariable<T>(                              \
        const std::string &name, const ShapeID shapeID, const Dims ldims,      \
        const Dims gdims, const Dims offsets, const T *values);
ADIOS2_FOREACH_TYPE_1ARG(declare_template_instantiation)
#undef declare_template_instantiation

} // end namespace interop
} // end namespace adios
