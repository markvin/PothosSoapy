// Copyright (c) 2026 Pothos contributors
// SPDX-License-Identifier: BSL-1.0

#pragma once
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Registry.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

/***********************************************************************
 * A SoapySDR driver for the self-tests of the soapy blocks.
 * It is never a SoapySDR module: a test registers it with a
 * ScopedTestDriver, and the registration ends with the scope.
 * TX channel i writes into a FIFO, RX channel i reads from it.
 **********************************************************************/
namespace SoapyTest {

inline constexpr const char *DriverName = "pothos_soapy_test";

struct DriverState;

class TestDevice : public SoapySDR::Device
{
public:
    explicit TestDevice(DriverState &state);
    ~TestDevice(void) override;

    //identification
    std::string getDriverKey(void) const override;
    std::string getHardwareKey(void) const override;
    size_t getNumChannels(const int direction) const override;
    bool getFullDuplex(const int direction, const size_t channel) const override;

    //streams
    std::vector<std::string> getStreamFormats(const int direction, const size_t channel) const override;
    std::string getNativeStreamFormat(const int direction, const size_t channel, double &fullScale) const override;
    SoapySDR::Stream *setupStream(const int direction, const std::string &format,
        const std::vector<size_t> &channels, const SoapySDR::Kwargs &args) override;
    void closeStream(SoapySDR::Stream *stream) override;
    size_t getStreamMTU(SoapySDR::Stream *stream) const override;
    int activateStream(SoapySDR::Stream *stream, const int flags, const long long timeNs, const size_t numElems) override;
    int deactivateStream(SoapySDR::Stream *stream, const int flags, const long long timeNs) override;
    int readStream(SoapySDR::Stream *stream, void * const *buffs, const size_t numElems,
        int &flags, long long &timeNs, const long timeoutUs) override;
    int writeStream(SoapySDR::Stream *stream, const void * const *buffs, const size_t numElems,
        int &flags, const long long timeNs, const long timeoutUs) override;

    //antennas
    std::vector<std::string> listAntennas(const int direction, const size_t channel) const override;
    void setAntenna(const int direction, const size_t channel, const std::string &name) override;
    std::string getAntenna(const int direction, const size_t channel) const override;

    //frontend corrections
    bool hasDCOffsetMode(const int direction, const size_t channel) const override;
    void setDCOffsetMode(const int direction, const size_t channel, const bool automatic) override;
    bool getDCOffsetMode(const int direction, const size_t channel) const override;

    //gain (the overall gain is distributed by SoapySDR::Device)
    std::vector<std::string> listGains(const int direction, const size_t channel) const override;
    bool hasGainMode(const int direction, const size_t channel) const override;
    void setGainMode(const int direction, const size_t channel, const bool automatic) override;
    bool getGainMode(const int direction, const size_t channel) const override;
    void setGain(const int direction, const size_t channel, const std::string &name, const double value) override;
    double getGain(const int direction, const size_t channel, const std::string &name) const override;
    SoapySDR::Range getGainRange(const int direction, const size_t channel, const std::string &name) const override;
    using SoapySDR::Device::setGain;
    using SoapySDR::Device::getGain;
    using SoapySDR::Device::getGainRange;

    //frequency (one component "RF")
    void setFrequency(const int direction, const size_t channel, const std::string &name,
        const double frequency, const SoapySDR::Kwargs &args) override;
    double getFrequency(const int direction, const size_t channel, const std::string &name) const override;
    std::vector<std::string> listFrequencies(const int direction, const size_t channel) const override;
    SoapySDR::RangeList getFrequencyRange(const int direction, const size_t channel, const std::string &name) const override;
    using SoapySDR::Device::setFrequency;
    using SoapySDR::Device::getFrequency;
    using SoapySDR::Device::getFrequencyRange;

    //sample rate and bandwidth
    void setSampleRate(const int direction, const size_t channel, const double rate) override;
    double getSampleRate(const int direction, const size_t channel) const override;
    SoapySDR::RangeList getSampleRateRange(const int direction, const size_t channel) const override;
    void setBandwidth(const int direction, const size_t channel, const double bandwidth) override;
    double getBandwidth(const int direction, const size_t channel) const override;
    SoapySDR::RangeList getBandwidthRange(const int direction, const size_t channel) const override;

    //clocking and time
    void setMasterClockRate(const double rate) override;
    double getMasterClockRate(void) const override;
    std::vector<std::string> listClockSources(void) const override;
    void setClockSource(const std::string &source) override;
    std::string getClockSource(void) const override;
    std::vector<std::string> listTimeSources(void) const override;
    void setTimeSource(const std::string &source) override;
    std::string getTimeSource(void) const override;
    bool hasHardwareTime(const std::string &what) const override;
    long long getHardwareTime(const std::string &what) const override;
    void setHardwareTime(const long long timeNs, const std::string &what) override;

    //sensors and settings
    std::vector<std::string> listSensors(void) const override;
    std::string readSensor(const std::string &key) const override;
    void writeSetting(const std::string &key, const std::string &value) override;
    std::string readSetting(const std::string &key) const override;
    void writeSetting(const int direction, const size_t channel, const std::string &key, const std::string &value) override;
    std::string readSetting(const int direction, const size_t channel, const std::string &key) const override;

private:
    struct Channel
    {
        double frequency = 0.0;
        double sampleRate = 1e6;
        double bandwidth = 0.0;
        bool gainMode = false;
        bool dcOffsetMode = false;
        std::string antenna;
        std::map<std::string, double> gains;
        std::map<std::string, std::string> settings;
    };
    Channel &channel(const int direction, const size_t channel);
    const Channel &channel(const int direction, const size_t channel) const;

    DriverState &_state;
    mutable std::mutex _mutex;
    std::map<std::pair<int, size_t>, Channel> _channels;
    double _masterClockRate;
    std::string _clockSource;
    std::string _timeSource;
    long long _hardwareTime;
    std::map<std::string, std::string> _settings;
};

//! Registers the test driver while the object exists (one at a time)
class ScopedTestDriver
{
public:
    ScopedTestDriver(void);
    ~ScopedTestDriver(void);
    ScopedTestDriver(const ScopedTestDriver &) = delete;
    ScopedTestDriver &operator=(const ScopedTestDriver &) = delete;

    //! The last device that SoapySDR made in this scope, or null
    TestDevice *lastDevice(void) const;

private:
    std::unique_ptr<DriverState> _state;
    std::unique_ptr<SoapySDR::Registry> _registry;
};

}
