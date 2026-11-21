// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include "RobotiqTactileSensor.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <Robotiq/common/serial_testing/pty_peer.hpp>
#endif

namespace {
using namespace std::chrono_literals;

#ifdef _WIN32
const std::string MISSING_PORT = "COM255";
#else
const std::string MISSING_PORT = "/dev/this_should_not_exist";
#endif

const std::vector<uint8_t> GET_VERSION = {0x9A, 0x00, 0xE3, 0x00};

std::vector<uint8_t> autosend(uint8_t period_ms)
{
    return {0x9A, 0x00, 0x58, 0x01, period_ms};
}

// A sample frame: a temperature reading of \p value for finger 0.
std::vector<uint8_t> temperature(uint16_t value)
{
    return {0x9A, 0x00, 0x58, 0x03, 0x60, static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF)};
}

std::vector<uint8_t> versionReply(const std::string& version)
{
    std::vector<uint8_t> frame = {0x9A, 0x00, 0xE3, static_cast<uint8_t>(version.size())};
    frame.insert(frame.end(), version.begin(), version.end());
    return frame;
}

// The callback is a plain function pointer: it reports through globals.
std::atomic<int> g_samples(0);
std::atomic<int> g_temperature(0);

void onSample(const Fingers& fingers)
{
    g_temperature = fingers.finger[0].temperature;
    ++g_samples;
}

template <typename Condition>
bool waitFor(Condition condition, std::chrono::milliseconds timeout = 10s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition())
    {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

TEST(TactileSensor, OpenFailureReportsTheTransportMessage)
{
    RobotiqTactileSensor sensor(MISSING_PORT.c_str(), onSample);

    EXPECT_FALSE(sensor.isConnected());
    EXPECT_NE(std::string(sensor.getLastError()).find("'" + MISSING_PORT + "'"), std::string::npos)
        << sensor.getLastError();
}

#ifndef _WIN32
using Robotiq::common::serial_testing::PtyPeer;

class TactileSensorOverPty : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!m_hub.valid())
            GTEST_SKIP() << "no pseudo-terminal available on this machine";
        g_samples = 0;
        g_temperature = 0;
    }

    void connect(unsigned int period_ms = 1)
    {
        m_sdk.reset(new RobotiqTactileSensor(m_hub.slavePath().c_str(), onSample, period_ms));
        ASSERT_TRUE(m_sdk->isConnected()) << m_sdk->getLastError();
        ASSERT_EQ(m_hub.receive(5), autosend(static_cast<uint8_t>(period_ms)));
    }

    // A macOS pty holds what the SDK writes until the hub reads it: a
    // request left unread fails its drain.
    std::future<std::vector<uint8_t> > hubReceiveAsync(size_t size)
    {
        return std::async(std::launch::async, [this, size] { return m_hub.receive(size); });
    }

    PtyPeer m_hub;
    std::unique_ptr<RobotiqTactileSensor> m_sdk;
};

TEST_F(TactileSensorOverPty, StartsAndStopsStreaming)
{
    connect(5);

    // The hub reads while the SDK shuts down: a macOS pty holds written
    // bytes until then, and drops them when the port closes.
    std::future<std::vector<uint8_t> > stopFrame = std::async(std::launch::async, [this] {
        return m_hub.receive(5);
    });
    m_sdk.reset();

    EXPECT_EQ(stopFrame.get(), autosend(0));
}

TEST_F(TactileSensorOverPty, DeliversSamples)
{
    connect();

    ASSERT_TRUE(m_hub.send(temperature(0x0123)));

    EXPECT_TRUE(waitFor([] { return g_samples > 0; }));
    EXPECT_EQ(g_temperature, 0x0123);
}

TEST_F(TactileSensorOverPty, ReadsTheFirmwareVersion)
{
    connect();

    std::string version;
    std::future<bool> answered = std::async(std::launch::async, [&] {
        return m_sdk->getFirmwareVersion(version);
    });
    ASSERT_EQ(m_hub.receive(GET_VERSION.size()), GET_VERSION);
    ASSERT_TRUE(m_hub.send(versionReply("2.3.1")));

    EXPECT_TRUE(answered.get()) << m_sdk->getLastError();
    EXPECT_EQ(version, "2.3.1");
}

TEST_F(TactileSensorOverPty, LosingTheSensorDisconnectsAndReportsWhy)
{
    connect();

    // So the loss has a rate to zero.
    std::atomic<bool> streaming(true);
    std::thread sensorSide([&] {
        while (streaming && m_hub.send(temperature(1)))
            std::this_thread::sleep_for(1ms);
    });
    const bool rated = waitFor([this] { return m_sdk->getDataRate() > 0; });
    streaming = false;
    sensorSide.join();
    ASSERT_TRUE(rated);

    m_hub.hangUp();

    EXPECT_TRUE(waitFor([this] { return !m_sdk->isConnected(); }));
    EXPECT_EQ(m_sdk->getDataRate(), 0U);
    EXPECT_EQ(std::string(m_sdk->getLastError()).rfind("Connection lost: ", 0), 0U)
        << m_sdk->getLastError();
}

TEST_F(TactileSensorOverPty, AFirmwareVersionQueryEndsWhenTheSensorIsLost)
{
    connect();

    std::string version;
    std::future<bool> answered = std::async(std::launch::async, [&] {
        return m_sdk->getFirmwareVersion(version, 10000);
    });
    ASSERT_EQ(m_hub.receive(GET_VERSION.size()), GET_VERSION);
    m_hub.hangUp();

    ASSERT_EQ(answered.wait_for(10s), std::future_status::ready);
    EXPECT_FALSE(answered.get());
    EXPECT_FALSE(m_sdk->isConnected());
}

TEST_F(TactileSensorOverPty, AFirmwareVersionQueryNeedsCollectionRunning)
{
    connect();
    m_sdk->stop();

    std::string version;
    EXPECT_FALSE(m_sdk->getFirmwareVersion(version));
    EXPECT_STREQ(m_sdk->getLastError(), "Cannot query firmware version: data collection is stopped");
}

TEST_F(TactileSensorOverPty, AFirmwareVersionQueryTimesOutWithoutAReply)
{
    connect();

    std::string version;
    std::future<std::vector<uint8_t> > request = hubReceiveAsync(GET_VERSION.size());
    EXPECT_FALSE(m_sdk->getFirmwareVersion(version, 20));
    EXPECT_STREQ(m_sdk->getLastError(), "Timeout waiting for firmware version reply");
    EXPECT_EQ(request.get(), GET_VERSION);
}

TEST_F(TactileSensorOverPty, ALateFirmwareVersionReplyIsNotServedToTheNextQuery)
{
    connect();
    std::string version;
    std::future<std::vector<uint8_t> > request = hubReceiveAsync(GET_VERSION.size());
    ASSERT_FALSE(m_sdk->getFirmwareVersion(version, 20));
    ASSERT_EQ(request.get(), GET_VERSION);

    // The reply to the query that timed out, then a sample: once the sample
    // is in, the read thread has handled the reply too.
    ASSERT_TRUE(m_hub.send(versionReply("1.0.0")));
    ASSERT_TRUE(m_hub.send(temperature(1)));
    ASSERT_TRUE(waitFor([] { return g_samples > 0; }));

    request = hubReceiveAsync(GET_VERSION.size());
    EXPECT_FALSE(m_sdk->getFirmwareVersion(version, 20));
    EXPECT_STREQ(m_sdk->getLastError(), "Timeout waiting for firmware version reply");
    EXPECT_EQ(request.get(), GET_VERSION);
}

TEST_F(TactileSensorOverPty, StopEndsAWaitingFirmwareVersionQuery)
{
    connect();

    std::string version;
    std::future<bool> answered = std::async(std::launch::async, [&] {
        return m_sdk->getFirmwareVersion(version, 10000);
    });
    ASSERT_EQ(m_hub.receive(GET_VERSION.size()), GET_VERSION);
    // A macOS pty holds the stop frame until the hub reads it.
    std::future<std::vector<uint8_t> > stopFrame = std::async(std::launch::async, [this] {
        return m_hub.receive(5);
    });
    m_sdk->stop();

    ASSERT_EQ(answered.wait_for(10s), std::future_status::ready);
    EXPECT_FALSE(answered.get());
    EXPECT_STREQ(m_sdk->getLastError(), "Data collection stopped before the firmware version reply");
    EXPECT_EQ(stopFrame.get(), autosend(0));
}

TEST_F(TactileSensorOverPty, ReassemblesAFrameSplitAcrossReads)
{
    connect();
    const std::vector<uint8_t> frame = temperature(0x0123);

    // Repeated so some rounds split the frame across reads.
    for (int round = 1; round <= 16; ++round)
    {
        ASSERT_TRUE(m_hub.send(std::vector<uint8_t>(frame.begin(), frame.begin() + 3)));
        ASSERT_TRUE(m_hub.send(std::vector<uint8_t>(frame.begin() + 3, frame.end())));
        ASSERT_TRUE(waitFor([round] { return g_samples == round; }));
    }
    EXPECT_EQ(g_temperature, 0x0123);
}

TEST_F(TactileSensorOverPty, HandlesABurstLargerThanItsReadBuffer)
{
    connect();

    // 300 frames of 7 bytes: twice the 1024-byte receive buffer.
    std::vector<uint8_t> burst;
    for (int i = 0; i < 300; ++i)
    {
        const std::vector<uint8_t> frame = temperature(static_cast<uint16_t>(i));
        burst.insert(burst.end(), frame.begin(), frame.end());
    }
    ASSERT_TRUE(m_hub.send(burst));

    EXPECT_TRUE(waitFor([] { return g_samples == 300; })) << g_samples;
    EXPECT_EQ(g_temperature, 299);
}
#endif
} // namespace
