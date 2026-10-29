// Copyright (c) 2014-2017 Josh Blum
// SPDX-License-Identifier: BSL-1.0

#include <Pothos/Plugin.hpp>
#include <SoapySDR/Logger.hpp>
#include <Pothos/System/Logger.hpp>
#include <iostream>

/***********************************************************************
 * Register a Pothos logging handler for SoapySDR
 **********************************************************************/
using Pothos::System::LogLevel;
static_assert(SOAPY_SDR_FATAL == int(LogLevel::Fatal), "SoapySDR log levels match Pothos");
static_assert(SOAPY_SDR_INFO == int(LogLevel::Information), "SoapySDR log levels match Pothos");
static_assert(SOAPY_SDR_TRACE == int(LogLevel::Trace), "SoapySDR log levels match Pothos");

static void SoapyPothosLogHandler(const SoapySDR::LogLevel logLevel, const char *message)
{
    if (logLevel == SOAPY_SDR_SSI)
    {
        std::cerr << message << std::flush;
        return;
    }
    Pothos::System::Logger::log(LogLevel(logLevel), "SoapySDR", message);
}

pothos_static_block(registerSoapySDRLogHandler)
{
    SoapySDR::registerLogHandler(&SoapyPothosLogHandler);
}
