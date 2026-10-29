// Copyright (c) 2026 Pothos contributors
// SPDX-License-Identifier: BSL-1.0

#include "tests/TestDevice.hpp"
#include <Pothos/Testing.hpp>
#include <Pothos/Framework.hpp>
#include <Pothos/Proxy.hpp>
#include <Pothos/Plugin.hpp>
#include <SoapySDR/Device.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

using json = nlohmann::json;

static Pothos::ObjectKwargs testDeviceArgs(void)
{
    Pothos::ObjectKwargs args;
    args["driver"] = Pothos::Object(std::string(SoapyTest::DriverName));
    return args;
}

//the "Factories" field of /devices/soapy/info
static std::string soapyFactories(void)
{
    const auto info = Pothos::PluginRegistry::get("/devices/soapy/info")
        .getObject().extract<Pothos::Callable>().call<std::string>();
    return json::parse(info)["SoapySDR info"]["Factories"].get<std::string>();
}

static bool hasTestDriver(const SoapySDR::KwargsList &results)
{
    for (const auto &result : results)
    {
        const auto it = result.find("driver");
        if (it != result.end() and it->second == SoapyTest::DriverName) return true;
    }
    return false;
}

/***********************************************************************
 * The test driver is registered only inside the scope of a test
 **********************************************************************/
POTHOS_TEST_BLOCK("/soapy/tests", test_driver_registration)
{
    {
        SoapyTest::ScopedTestDriver driver;
        POTHOS_TEST_TRUE(soapyFactories().find(SoapyTest::DriverName) != std::string::npos);
        POTHOS_TEST_TRUE(not hasTestDriver(SoapySDR::Device::enumerate()));
        POTHOS_TEST_EQUAL(SoapySDR::Device::enumerate(std::string("driver=") + SoapyTest::DriverName).size(), 1);
    }
    POTHOS_TEST_TRUE(soapyFactories().find(SoapyTest::DriverName) == std::string::npos);
}

/***********************************************************************
 * Data from /soapy/sink through the test driver to /soapy/source
 **********************************************************************/
static void testLoopback(const std::string &dtype, const std::vector<size_t> &channels)
{
    std::cout << "loopback " << dtype << " with " << channels.size() << " channel(s)" << std::endl;
    SoapyTest::ScopedTestDriver driver;
    const size_t numElems = 4096;

    auto sink = Pothos::BlockRegistry::make("/soapy/sink", dtype, channels);
    sink.call("setupDevice", testDeviceArgs());
    sink.call("setupStream", Pothos::ObjectKwargs());
    auto source = Pothos::BlockRegistry::make("/soapy/source", dtype, channels);
    source.call("setupDevice", testDeviceArgs());
    source.call("setupStream", Pothos::ObjectKwargs());

    //known and different data for each channel
    std::vector<Pothos::Proxy> feeders, collectors;
    std::vector<Pothos::BufferChunk> sent;
    for (size_t i = 0; i < channels.size(); i++)
    {
        feeders.push_back(Pothos::BlockRegistry::make("/blocks/feeder_source", dtype));
        collectors.push_back(Pothos::BlockRegistry::make("/blocks/collector_sink", dtype));
        Pothos::BufferChunk buff(dtype, numElems);
        auto bytes = buff.as<unsigned char *>();
        for (size_t j = 0; j < buff.length; j++) bytes[j] = static_cast<unsigned char>(j*31 + i*7 + 1);
        feeders.back().call("feedBuffer", buff);
        sent.push_back(buff);
    }

    Pothos::Topology topology;
    for (size_t i = 0; i < channels.size(); i++)
    {
        topology.connect(feeders[i], 0, sink, i);
        topology.connect(source, i, collectors[i], 0);
    }
    topology.commit();

    //the source gives only timeouts until the sink writes: wait for the data
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (size_t i = 0; i < channels.size(); i++)
    {
        while (collectors[i].call<Pothos::BufferChunk>("getBuffer").elements() < numElems and
            std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    for (size_t i = 0; i < channels.size(); i++)
    {
        const auto received = collectors[i].call<Pothos::BufferChunk>("getBuffer");
        POTHOS_TEST_EQUAL(received.elements(), numElems);
        POTHOS_TEST_TRUE(std::memcmp(received.as<const void *>(), sent[i].as<const void *>(), sent[i].length) == 0);
    }
}

POTHOS_TEST_BLOCK("/soapy/tests", test_loopback_cf32)
{
    testLoopback("complex_float32", {0});
}

POTHOS_TEST_BLOCK("/soapy/tests", test_loopback_cs16)
{
    testLoopback("complex_int16", {0});
}

POTHOS_TEST_BLOCK("/soapy/tests", test_loopback_two_channels)
{
    testLoopback("complex_float32", {0, 1});
}

/***********************************************************************
 * The setters of the blocks reach the driver, the getters read it back
 **********************************************************************/
static void testSettings(const std::string &path, const int direction, const std::string &antenna)
{
    std::cout << "settings of " << path << std::endl;
    SoapyTest::ScopedTestDriver driver;
    auto block = Pothos::BlockRegistry::make(path, "complex_float32", std::vector<size_t>{0});
    block.call("setupDevice", testDeviceArgs());
    auto device = driver.lastDevice();
    POTHOS_TEST_TRUE(device != nullptr);

    block.call("setSampleRate", 2e6);
    POTHOS_TEST_EQUAL(block.call<double>("getSampleRate"), 2e6);
    POTHOS_TEST_EQUAL(device->getSampleRate(direction, 0), 2e6);

    block.call("setFrequency", 100e6);
    POTHOS_TEST_EQUAL(block.call<double>("getFrequency0"), 100e6);
    POTHOS_TEST_EQUAL(device->getFrequency(direction, 0), 100e6);

    block.call("setGain", 20.0);
    POTHOS_TEST_EQUAL(block.call<double>("getGain0"), 20.0);
    Pothos::ObjectMap gainMap;
    gainMap[Pothos::Object(std::string("LNA"))] = Pothos::Object(10.0);
    block.call("setGain", gainMap);
    POTHOS_TEST_EQUAL(block.call<double>("getGain0", std::string("LNA")), 10.0);
    POTHOS_TEST_EQUAL(device->getGain(direction, 0, "LNA"), 10.0);

    block.call("setGainMode", true);
    //upstream getGainMode<i> gives a double (block API: no change)
    POTHOS_TEST_EQUAL(block.call<double>("getGainMode0"), 1.0);
    POTHOS_TEST_TRUE(device->getGainMode(direction, 0));

    const std::vector<std::string> antennas{antenna + "1", antenna + "2"};
    POTHOS_TEST_EQUALV(block.call<std::vector<std::string>>("getAntennas0"), antennas);
    block.call("setAntenna", antennas[1]);
    POTHOS_TEST_EQUAL(block.call<std::string>("getAntenna0"), antennas[1]);

    block.call("setBandwidth", 1e6);
    POTHOS_TEST_EQUAL(block.call<double>("getBandwidth0"), 1e6);

    block.call("setDCOffsetMode", true);
    POTHOS_TEST_TRUE(block.call<bool>("getDCOffsetMode0"));

    block.call("setClockSource", std::string("external"));
    POTHOS_TEST_EQUAL(block.call<std::string>("getClockSource"), "external");

    block.call("setGlobalSetting", std::string("test_key"), Pothos::Object(std::string("test_value")));
    POTHOS_TEST_EQUAL(device->readSetting("test_key"), "test_value");

    //an error of the driver goes to the caller
    Pothos::ObjectMap badGain;
    badGain[Pothos::Object(std::string("NO_SUCH_ELEMENT"))] = Pothos::Object(1.0);
    POTHOS_TEST_THROWS(block.call("setGain", badGain), std::exception);
}

POTHOS_TEST_BLOCK("/soapy/tests", test_settings)
{
    testSettings("/soapy/source", SOAPY_SDR_RX, "RX");
    testSettings("/soapy/sink", SOAPY_SDR_TX, "TX");
}
