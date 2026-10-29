// Copyright (c) 2026 Pothos contributors
// SPDX-License-Identifier: BSL-1.0

#include <Pothos/Testing.hpp>
#include <Pothos/Framework.hpp>
#include <Pothos/Proxy.hpp>
#include <SoapySDR/Types.hpp>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

/***********************************************************************
 * A manual check with a real device: CTest test soapy.device, only with
 * -DSOAPY_TEST_DEVICE=<device args>. Not under /soapy/tests: the normal
 * self-tests must not need a device.
 **********************************************************************/
POTHOS_TEST_BLOCK("/soapy/device_tests", test_source)
{
    const char *markup = std::getenv("SOAPY_TEST_DEVICE");
    //PothosUtil --self-tests runs all tests: without a device, only say so
    if (markup == nullptr or *markup == '\0')
    {
        std::cout << "SOAPY_TEST_DEVICE is not set: no device to test" << std::endl;
        return;
    }

    Pothos::ObjectKwargs deviceArgs;
    for (const auto &pair : SoapySDR::KwargsFromString(markup))
    {
        deviceArgs[pair.first] = Pothos::Object(pair.second);
    }
    const double rate = 1e6;

    auto source = Pothos::BlockRegistry::make("/soapy/source", "complex_float32", std::vector<size_t>{0});
    source.call("setupDevice", deviceArgs);
    source.call("setSampleRate", rate);
    source.call("setupStream", Pothos::ObjectKwargs());
    auto collector = Pothos::BlockRegistry::make("/blocks/collector_sink", "complex_float32");

    size_t numElems = 0;
    double seconds = 0.0;
    {
        Pothos::Topology topology;
        topology.connect(source, 0, collector, 0);
        topology.commit();
        const auto start = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        numElems = collector.call<Pothos::BufferChunk>("getBuffer").elements();
        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

    std::cout << "soapy.device: " << numElems << " samples in " << seconds << " s ("
        << numElems/seconds << " Sps, requested " << rate << " Sps)" << std::endl;
    POTHOS_TEST_TRUE(numElems > 0);
}
