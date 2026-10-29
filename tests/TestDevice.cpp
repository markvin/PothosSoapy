// Copyright (c) 2026 Pothos contributors
// SPDX-License-Identifier: BSL-1.0

#include "tests/TestDevice.hpp"
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Time.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <stdexcept>

namespace SoapyTest {

static constexpr size_t NumChannels = 2;
static constexpr size_t StreamMTU = 1024;

//the state of one scope: a FIFO from TX channel i to RX channel i
struct DriverState
{
    std::mutex mutex;
    std::condition_variable cond;
    std::vector<std::deque<unsigned char>> fifos{NumChannels};
    TestDevice *lastDevice = nullptr;
};

struct TestStream
{
    int direction;
    std::vector<size_t> channels;
    size_t elemSize;
    long long ticks;
};

//SoapySDR::Registry takes function pointers: the scope sets this state.
//Find can run on other threads (EnumerateCache), so the pointer is atomic.
static std::atomic<DriverState *> activeState{nullptr};

static SoapySDR::KwargsList findTestDevice(const SoapySDR::Kwargs &args)
{
    //never in a search without the driver name (EnumerateCache, the overlay)
    const auto it = args.find("driver");
    if (activeState == nullptr or it == args.end() or it->second != DriverName) return {};
    SoapySDR::Kwargs result;
    result["driver"] = DriverName;
    result["label"] = "Pothos soapy test device";
    return {result};
}

static SoapySDR::Device *makeTestDevice(const SoapySDR::Kwargs &)
{
    auto state = activeState.load();
    if (state == nullptr) throw std::runtime_error("the soapy test driver is not registered");
    auto device = new TestDevice(*state);
    std::lock_guard<std::mutex> lock(state->mutex);
    state->lastDevice = device;
    return device;
}

static void checkChannel(const size_t channel)
{
    if (channel >= NumChannels) throw std::invalid_argument("no channel " + std::to_string(channel));
}

static void checkOption(const std::vector<std::string> &options, const std::string &value, const std::string &what)
{
    if (std::find(options.begin(), options.end(), value) == options.end())
    {
        throw std::invalid_argument("unknown " + what + " " + value);
    }
}

/***********************************************************************
 * ScopedTestDriver
 **********************************************************************/
ScopedTestDriver::ScopedTestDriver(void):
    _state(new DriverState())
{
    DriverState *expected = nullptr;
    if (not activeState.compare_exchange_strong(expected, _state.get()))
    {
        throw std::logic_error("the soapy test driver is already registered");
    }
    _registry.reset(new SoapySDR::Registry(DriverName, &findTestDevice, &makeTestDevice, SOAPY_SDR_ABI_VERSION));
}

ScopedTestDriver::~ScopedTestDriver(void)
{
    _registry.reset();
    activeState = nullptr;
}

TestDevice *ScopedTestDriver::lastDevice(void) const
{
    std::lock_guard<std::mutex> lock(_state->mutex);
    return _state->lastDevice;
}

/***********************************************************************
 * TestDevice: identification
 **********************************************************************/
TestDevice::TestDevice(DriverState &state):
    _state(state),
    _masterClockRate(10e6),
    _clockSource("internal"),
    _timeSource("internal"),
    _hardwareTime(0)
{
    return;
}

TestDevice::~TestDevice(void)
{
    std::lock_guard<std::mutex> lock(_state.mutex);
    if (_state.lastDevice == this) _state.lastDevice = nullptr;
}

std::string TestDevice::getDriverKey(void) const
{
    return DriverName;
}

std::string TestDevice::getHardwareKey(void) const
{
    return DriverName;
}

size_t TestDevice::getNumChannels(const int) const
{
    return NumChannels;
}

bool TestDevice::getFullDuplex(const int, const size_t) const
{
    return true;
}

TestDevice::Channel &TestDevice::channel(const int direction, const size_t chan)
{
    checkChannel(chan);
    return _channels[{direction, chan}];
}

const TestDevice::Channel &TestDevice::channel(const int direction, const size_t chan) const
{
    checkChannel(chan);
    static const Channel defaults;
    const auto it = _channels.find({direction, chan});
    return (it == _channels.end())? defaults : it->second;
}

/***********************************************************************
 * TestDevice: streams
 **********************************************************************/
std::vector<std::string> TestDevice::getStreamFormats(const int, const size_t) const
{
    return {SOAPY_SDR_CF32, SOAPY_SDR_CS16};
}

std::string TestDevice::getNativeStreamFormat(const int, const size_t, double &fullScale) const
{
    fullScale = 1.0;
    return SOAPY_SDR_CF32;
}

SoapySDR::Stream *TestDevice::setupStream(const int direction, const std::string &format,
    const std::vector<size_t> &channels, const SoapySDR::Kwargs &)
{
    checkOption(this->getStreamFormats(direction, 0), format, "stream format");
    std::unique_ptr<TestStream> stream(new TestStream());
    stream->direction = direction;
    stream->channels = channels.empty()? std::vector<size_t>{0} : channels;
    for (const auto chan : stream->channels) checkChannel(chan);
    stream->elemSize = SoapySDR::formatToSize(format);
    stream->ticks = 0;
    return reinterpret_cast<SoapySDR::Stream *>(stream.release());
}

void TestDevice::closeStream(SoapySDR::Stream *stream)
{
    delete reinterpret_cast<TestStream *>(stream);
}

size_t TestDevice::getStreamMTU(SoapySDR::Stream *) const
{
    return StreamMTU;
}

int TestDevice::activateStream(SoapySDR::Stream *, const int flags, const long long, const size_t)
{
    if ((flags & ~(SOAPY_SDR_HAS_TIME | SOAPY_SDR_END_BURST)) != 0) return SOAPY_SDR_NOT_SUPPORTED;
    return 0;
}

int TestDevice::deactivateStream(SoapySDR::Stream *, const int flags, const long long)
{
    if ((flags & ~SOAPY_SDR_HAS_TIME) != 0) return SOAPY_SDR_NOT_SUPPORTED;
    return 0;
}

int TestDevice::readStream(SoapySDR::Stream *handle, void * const *buffs, const size_t numElems,
    int &flags, long long &timeNs, const long timeoutUs)
{
    auto stream = reinterpret_cast<TestStream *>(handle);
    if (stream->direction != SOAPY_SDR_RX) return SOAPY_SDR_STREAM_ERROR;

    //the number of elements that all channels of the stream have
    const auto available = [&]
    {
        size_t bytes = SIZE_MAX;
        for (const auto chan : stream->channels) bytes = std::min(bytes, _state.fifos[chan].size());
        return std::min(numElems, bytes/stream->elemSize);
    };

    std::unique_lock<std::mutex> lock(_state.mutex);
    _state.cond.wait_for(lock, std::chrono::microseconds(timeoutUs), [&]{return available() != 0;});
    const size_t n = available();
    if (n == 0) return SOAPY_SDR_TIMEOUT;

    for (size_t i = 0; i < stream->channels.size(); i++)
    {
        auto &fifo = _state.fifos[stream->channels[i]];
        const auto end = fifo.begin() + n*stream->elemSize;
        std::copy(fifo.begin(), end, static_cast<unsigned char *>(buffs[i]));
        fifo.erase(fifo.begin(), end);
    }
    lock.unlock();

    flags = SOAPY_SDR_HAS_TIME;
    timeNs = SoapySDR::ticksToTimeNs(stream->ticks, this->getSampleRate(SOAPY_SDR_RX, stream->channels.front()));
    stream->ticks += n;
    return int(n);
}

int TestDevice::writeStream(SoapySDR::Stream *handle, const void * const *buffs, const size_t numElems,
    int &, const long long, const long)
{
    auto stream = reinterpret_cast<TestStream *>(handle);
    if (stream->direction != SOAPY_SDR_TX) return SOAPY_SDR_STREAM_ERROR;

    {
        std::lock_guard<std::mutex> lock(_state.mutex);
        for (size_t i = 0; i < stream->channels.size(); i++)
        {
            const auto bytes = static_cast<const unsigned char *>(buffs[i]);
            auto &fifo = _state.fifos[stream->channels[i]];
            fifo.insert(fifo.end(), bytes, bytes + numElems*stream->elemSize);
        }
    }
    _state.cond.notify_all();
    return int(numElems);
}

/***********************************************************************
 * TestDevice: antennas and frontend corrections
 **********************************************************************/
std::vector<std::string> TestDevice::listAntennas(const int direction, const size_t) const
{
    if (direction == SOAPY_SDR_TX) return {"TX1", "TX2"};
    return {"RX1", "RX2"};
}

void TestDevice::setAntenna(const int direction, const size_t chan, const std::string &name)
{
    checkOption(this->listAntennas(direction, chan), name, "antenna");
    std::lock_guard<std::mutex> lock(_mutex);
    this->channel(direction, chan).antenna = name;
}

std::string TestDevice::getAntenna(const int direction, const size_t chan) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto &antenna = this->channel(direction, chan).antenna;
    return antenna.empty()? this->listAntennas(direction, chan).front() : antenna;
}

bool TestDevice::hasDCOffsetMode(const int, const size_t) const
{
    return true;
}

void TestDevice::setDCOffsetMode(const int direction, const size_t chan, const bool automatic)
{
    std::lock_guard<std::mutex> lock(_mutex);
    this->channel(direction, chan).dcOffsetMode = automatic;
}

bool TestDevice::getDCOffsetMode(const int direction, const size_t chan) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return this->channel(direction, chan).dcOffsetMode;
}

/***********************************************************************
 * TestDevice: gain
 **********************************************************************/
std::vector<std::string> TestDevice::listGains(const int, const size_t) const
{
    return {"LNA", "PGA"};
}

bool TestDevice::hasGainMode(const int, const size_t) const
{
    return true;
}

void TestDevice::setGainMode(const int direction, const size_t chan, const bool automatic)
{
    std::lock_guard<std::mutex> lock(_mutex);
    this->channel(direction, chan).gainMode = automatic;
}

bool TestDevice::getGainMode(const int direction, const size_t chan) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return this->channel(direction, chan).gainMode;
}

void TestDevice::setGain(const int direction, const size_t chan, const std::string &name, const double value)
{
    checkOption(this->listGains(direction, chan), name, "gain element");
    std::lock_guard<std::mutex> lock(_mutex);
    this->channel(direction, chan).gains[name] = value;
}

double TestDevice::getGain(const int direction, const size_t chan, const std::string &name) const
{
    checkOption(this->listGains(direction, chan), name, "gain element");
    std::lock_guard<std::mutex> lock(_mutex);
    const auto &gains = this->channel(direction, chan).gains;
    const auto it = gains.find(name);
    return (it == gains.end())? 0.0 : it->second;
}

SoapySDR::Range TestDevice::getGainRange(const int direction, const size_t chan, const std::string &name) const
{
    checkOption(this->listGains(direction, chan), name, "gain element");
    return SoapySDR::Range(0.0, 30.0);
}

/***********************************************************************
 * TestDevice: frequency, sample rate, bandwidth
 **********************************************************************/
void TestDevice::setFrequency(const int direction, const size_t chan, const std::string &name,
    const double frequency, const SoapySDR::Kwargs &)
{
    checkOption(this->listFrequencies(direction, chan), name, "frequency component");
    std::lock_guard<std::mutex> lock(_mutex);
    this->channel(direction, chan).frequency = frequency;
}

double TestDevice::getFrequency(const int direction, const size_t chan, const std::string &name) const
{
    checkOption(this->listFrequencies(direction, chan), name, "frequency component");
    std::lock_guard<std::mutex> lock(_mutex);
    return this->channel(direction, chan).frequency;
}

std::vector<std::string> TestDevice::listFrequencies(const int, const size_t) const
{
    return {"RF"};
}

SoapySDR::RangeList TestDevice::getFrequencyRange(const int, const size_t, const std::string &) const
{
    return {SoapySDR::Range(0.0, 6e9)};
}

void TestDevice::setSampleRate(const int direction, const size_t chan, const double rate)
{
    if (rate <= 0.0) throw std::invalid_argument("the sample rate must be positive");
    std::lock_guard<std::mutex> lock(_mutex);
    this->channel(direction, chan).sampleRate = rate;
}

double TestDevice::getSampleRate(const int direction, const size_t chan) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return this->channel(direction, chan).sampleRate;
}

SoapySDR::RangeList TestDevice::getSampleRateRange(const int, const size_t) const
{
    return {SoapySDR::Range(1e3, 100e6)};
}

void TestDevice::setBandwidth(const int direction, const size_t chan, const double bandwidth)
{
    std::lock_guard<std::mutex> lock(_mutex);
    this->channel(direction, chan).bandwidth = bandwidth;
}

double TestDevice::getBandwidth(const int direction, const size_t chan) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return this->channel(direction, chan).bandwidth;
}

SoapySDR::RangeList TestDevice::getBandwidthRange(const int, const size_t) const
{
    return {SoapySDR::Range(1e3, 100e6)};
}

/***********************************************************************
 * TestDevice: clocking and time
 **********************************************************************/
void TestDevice::setMasterClockRate(const double rate)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _masterClockRate = rate;
}

double TestDevice::getMasterClockRate(void) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _masterClockRate;
}

std::vector<std::string> TestDevice::listClockSources(void) const
{
    return {"internal", "external"};
}

void TestDevice::setClockSource(const std::string &source)
{
    checkOption(this->listClockSources(), source, "clock source");
    std::lock_guard<std::mutex> lock(_mutex);
    _clockSource = source;
}

std::string TestDevice::getClockSource(void) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _clockSource;
}

std::vector<std::string> TestDevice::listTimeSources(void) const
{
    return {"internal", "external"};
}

void TestDevice::setTimeSource(const std::string &source)
{
    checkOption(this->listTimeSources(), source, "time source");
    std::lock_guard<std::mutex> lock(_mutex);
    _timeSource = source;
}

std::string TestDevice::getTimeSource(void) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _timeSource;
}

bool TestDevice::hasHardwareTime(const std::string &what) const
{
    return what.empty();
}

long long TestDevice::getHardwareTime(const std::string &) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _hardwareTime;
}

void TestDevice::setHardwareTime(const long long timeNs, const std::string &)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _hardwareTime = timeNs;
}

/***********************************************************************
 * TestDevice: sensors and settings
 **********************************************************************/
std::vector<std::string> TestDevice::listSensors(void) const
{
    return {"locked"};
}

std::string TestDevice::readSensor(const std::string &key) const
{
    checkOption(this->listSensors(), key, "sensor");
    return "true";
}

void TestDevice::writeSetting(const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _settings[key] = value;
}

std::string TestDevice::readSetting(const std::string &key) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _settings.find(key);
    return (it == _settings.end())? "" : it->second;
}

void TestDevice::writeSetting(const int direction, const size_t chan, const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> lock(_mutex);
    this->channel(direction, chan).settings[key] = value;
}

std::string TestDevice::readSetting(const int direction, const size_t chan, const std::string &key) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto &settings = this->channel(direction, chan).settings;
    const auto it = settings.find(key);
    return (it == settings.end())? "" : it->second;
}

}
