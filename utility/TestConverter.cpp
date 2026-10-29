// Copyright (c) 2026 Pothos contributors
// SPDX-License-Identifier: BSL-1.0

#include <Pothos/Testing.hpp>
#include <Pothos/Framework.hpp>
#include <Pothos/Proxy.hpp>
#include <cstdint>
#include <vector>

//the SoapySDR converter F32 -> S16 multiplies the input by the scalar,
//then by the full scale of S16 (32768)
POTHOS_TEST_BLOCK("/soapy/tests", test_converter)
{
    const std::vector<float> input{0.5f, -0.25f, 0.0f};
    const std::vector<int16_t> expected{8192, -4096, 0};

    Pothos::BufferChunk inputBuff("float32", input.size());
    for (size_t i = 0; i < input.size(); i++) inputBuff.as<float *>()[i] = input[i];

    auto feeder = Pothos::BlockRegistry::make("/blocks/feeder_source", "float32");
    auto converter = Pothos::BlockRegistry::make("/soapy/converter", "float32", "int16");
    auto collector = Pothos::BlockRegistry::make("/blocks/collector_sink", "int16");
    converter.call("setScalar", 0.5);
    POTHOS_TEST_EQUAL(converter.call<double>("getScalar"), 0.5);
    feeder.call("feedBuffer", inputBuff);

    {
        Pothos::Topology topology;
        topology.connect(feeder, 0, converter, 0);
        topology.connect(converter, 0, collector, 0);
        topology.commit();
        POTHOS_TEST_TRUE(topology.waitInactive());
    }

    const auto outputBuff = collector.call<Pothos::BufferChunk>("getBuffer");
    POTHOS_TEST_EQUAL(outputBuff.elements(), expected.size());
    POTHOS_TEST_EQUALA(outputBuff.as<const int16_t *>(), expected.data(), expected.size());

    //SoapySDR has no converter for this pair
    POTHOS_TEST_THROWS(
        Pothos::BlockRegistry::make("/soapy/converter", "uint8", "complex_float64"),
        Pothos::Exception);
}
