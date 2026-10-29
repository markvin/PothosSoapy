// Copyright (c) 2026 Pothos contributors
// SPDX-License-Identifier: BSL-1.0

#include <Pothos/Testing.hpp>
#include <Pothos/Framework.hpp>
#include <Pothos/Proxy.hpp>
#include <complex>
#include <cstdint>
#include <cstring>
#include <iostream>

using cint16 = std::complex<int16_t>;

//complex_int16 samples with the real part first, first+1, ...
static Pothos::BufferChunk rampBuffer(const int first, const size_t numElems)
{
    Pothos::BufferChunk buff("complex_int16", numElems);
    auto samps = buff.as<cint16 *>();
    for (size_t i = 0; i < numElems; i++) samps[i] = cint16(int16_t(first + int(i)), 0);
    return buff;
}

/***********************************************************************
 * Channel 1 starts 100 samples earlier than channel 0: the aligner drops
 * the first 100 samples of channel 1, then gives aligned outputs
 **********************************************************************/
POTHOS_TEST_BLOCK("/soapy/tests", test_channel_aligner)
{
    const double rate = 1e6;
    const long long timeNs = 1000000;
    const size_t numElems = 1000, offset = 100;

    auto aligner = Pothos::BlockRegistry::make("/soapy/channel_aligner");
    aligner.call("setNumChannels", size_t(2));

    std::vector<Pothos::Proxy> feeders, collectors;
    for (size_t ch = 0; ch < 2; ch++)
    {
        feeders.push_back(Pothos::BlockRegistry::make("/blocks/feeder_source", "complex_int16"));
        collectors.push_back(Pothos::BlockRegistry::make("/blocks/collector_sink", "complex_int16"));
        const long long startNs = (ch == 0)? timeNs : timeNs - static_cast<long long>(offset*1e9/rate);
        feeders[ch].call("feedLabel", Pothos::Label("rxRate", rate, 0));
        feeders[ch].call("feedLabel", Pothos::Label("rxTime", startNs, 0));
        //the same sample value at the same time on both channels
        feeders[ch].call("feedBuffer", rampBuffer((ch == 0)? 0 : -int(offset), numElems));
    }

    {
        Pothos::Topology topology;
        for (size_t ch = 0; ch < 2; ch++)
        {
            topology.connect(feeders[ch], 0, aligner, ch);
            topology.connect(aligner, ch, collectors[ch], 0);
        }
        topology.commit();
        POTHOS_TEST_TRUE(topology.waitInactive());
    }

    POTHOS_TEST_EQUAL(aligner.call<long long>("dropped"), (long long)offset);
    const auto out0 = collectors[0].call<Pothos::BufferChunk>("getBuffer");
    const auto out1 = collectors[1].call<Pothos::BufferChunk>("getBuffer");
    POTHOS_TEST_EQUAL(out0.elements(), numElems - offset);
    POTHOS_TEST_EQUAL(out1.elements(), numElems - offset);
    POTHOS_TEST_EQUAL(out0.as<const cint16 *>()[0].real(), 0);
    POTHOS_TEST_TRUE(std::memcmp(out0.as<const void *>(), out1.as<const void *>(), out0.length) == 0);
}

/***********************************************************************
 * Probability 0 forwards all samples, probability 1 drops all samples
 **********************************************************************/
static Pothos::BufferChunk runDropper(const double probability, const Pothos::BufferChunk &input)
{
    auto feeder = Pothos::BlockRegistry::make("/blocks/feeder_source", "complex_int16");
    auto dropper = Pothos::BlockRegistry::make("/soapy/random_dropper");
    auto collector = Pothos::BlockRegistry::make("/blocks/collector_sink", "complex_int16");
    dropper.call("setDropSize", size_t(100));
    dropper.call("setProbability", probability);
    POTHOS_TEST_EQUAL(dropper.call<double>("getProbability"), probability);
    feeder.call("feedBuffer", input);

    Pothos::Topology topology;
    topology.connect(feeder, 0, dropper, 0);
    topology.connect(dropper, 0, collector, 0);
    topology.commit();
    POTHOS_TEST_TRUE(topology.waitInactive());
    return collector.call<Pothos::BufferChunk>("getBuffer");
}

POTHOS_TEST_BLOCK("/soapy/tests", test_random_dropper)
{
    const auto input = rampBuffer(0, 1000);

    const auto all = runDropper(0.0, input);
    POTHOS_TEST_EQUAL(all.elements(), input.elements());
    POTHOS_TEST_TRUE(std::memcmp(all.as<const void *>(), input.as<const void *>(), input.length) == 0);

    std::cout << "the dropper prints D for each drop: ";
    const auto none = runDropper(1.0, input);
    std::cout << std::endl;
    POTHOS_TEST_EQUAL(none.elements(), 0);

    auto dropper = Pothos::BlockRegistry::make("/soapy/random_dropper");
    POTHOS_TEST_THROWS(dropper.call("setProbability", 1.5), Pothos::Exception);
}

/***********************************************************************
 * A frame start label gets a txTime label at the same index
 **********************************************************************/
POTHOS_TEST_BLOCK("/soapy/tests", test_tx_burst_timer)
{
    const long long hwTimeNs = 5000000000;
    const double timeDelta = 0.1;
    const size_t frameIndex = 10;

    auto feeder = Pothos::BlockRegistry::make("/blocks/feeder_source", "complex_int16");
    auto timer = Pothos::BlockRegistry::make("/soapy/tx_burst_timer");
    auto collector = Pothos::BlockRegistry::make("/blocks/collector_sink", "complex_int16");
    timer.call("setSampleRate", 1e6);
    timer.call("setTimeDelta", timeDelta);
    timer.call("setTime", hwTimeNs);
    feeder.call("feedLabel", Pothos::Label("frameStart", true, frameIndex));
    feeder.call("feedBuffer", rampBuffer(0, 100));

    {
        Pothos::Topology topology;
        topology.connect(feeder, 0, timer, 0);
        topology.connect(timer, 0, collector, 0);
        topology.commit();
        POTHOS_TEST_TRUE(topology.waitInactive());
    }

    POTHOS_TEST_EQUAL(collector.call<Pothos::BufferChunk>("getBuffer").elements(), 100);
    size_t numTxTime = 0;
    for (const auto &label : collector.call<std::vector<Pothos::Label>>("getLabels"))
    {
        if (label.id != "txTime") continue;
        numTxTime++;
        POTHOS_TEST_EQUAL(label.index, frameIndex);
        const auto txTimeNs = label.data.convert<long long>();
        POTHOS_TEST_TRUE(txTimeNs >= hwTimeNs + (long long)(timeDelta*1e9));
        POTHOS_TEST_TRUE(txTimeNs < hwTimeNs + (long long)(timeDelta*1e9) + 5000000000);
    }
    POTHOS_TEST_EQUAL(numTxTime, 1);
}
