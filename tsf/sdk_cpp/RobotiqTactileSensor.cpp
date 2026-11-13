// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2016 Shahbaz Youssefi
// Copyright (c) 2026 Robotiq
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the copyright holder nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

// Robotiq Tactile Sensor C++ SDK
// Based on Robotiq Tactile Sensor UI.

#include "RobotiqTactileSensor.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include <Robotiq/common/serial_error.hpp>
#include <Robotiq/common/serial_port.hpp>

// ============================================================================
// USB Protocol Definitions (Preserved from original communicator.cpp)
// ============================================================================

enum UsbPacketSpecial
{
    USB_PACKET_START_BYTE = 0x9A,
    USB_PACKET_HEADER_SIZE = 4,
    USB_PACKET_MAX_DATA_SIZE = 256,
    USB_PACKET_MAX_SIZE = USB_PACKET_HEADER_SIZE + USB_PACKET_MAX_DATA_SIZE,
};

enum UsbCommands
{
    USB_COMMAND_READ_SENSORS = 0x61,
    USB_COMMAND_AUTOSEND_SYNC_SENSORS = 0x58,
    USB_COMMAND_AUTOSEND_ASYNC_SENSORS = 0x59,
    USB_COMMAND_ENTER_BOOTLOADER = 0xE2,
    USB_COMMAND_GET_VERSION = 0xE3,
};

// Sensor types occupy the higher 4 bits, the 2 bits lower than that identify finger,
// and the lower 2 bits is used as an index.
enum UsbSensorType
{
    USB_SENSOR_TYPE_STATIC_TACTILE = 0x10,
    USB_SENSOR_TYPE_DYNAMIC_TACTILE = 0x20,
    USB_SENSOR_TYPE_ACCELEROMETER = 0x30,
    USB_SENSOR_TYPE_GYROSCOPE = 0x40,
    USB_SENSOR_TYPE_TEMPERATURE = 0x60,
    USB_SENSOR_TYPE_TIMESTAMP = 0x70,
};

struct UsbPacket
{
    uint8_t start_byte;
    uint8_t crc8;           // over command, data_length and data
    uint8_t command;        // 4 bits of flag (MSB) and 4 bits of command (LSB)
    uint8_t data_length;
    uint8_t data[USB_PACKET_MAX_DATA_SIZE];
};

// ============================================================================
// CRC-8 Implementation
// ============================================================================
// CRC-8 placeholder: the autosend command currently works without a valid CRC,
// so this returns a dummy value. Replace with a proper CRC-8 if the firmware
// begins enforcing checksums.

static uint8_t calcCrc8(uint8_t *data, size_t len)
{
    (void)len;
    return data[-1];
}



// ============================================================================
// USB Protocol Helper Functions (Preserved from original)
// ============================================================================

// The sensor's USB link: 115200 baud, 8N1, no flow control.
static const uint32_t USB_BAUDRATE = 115200;

// How long a frame may take to leave the port; the largest one takes ~23 ms
// at USB_BAUDRATE.
static const std::chrono::milliseconds USB_WRITE_TIMEOUT(100);

// How long one read waits for the first byte, so the read thread notices
// stop() and queued frames promptly.
static const std::chrono::milliseconds USB_READ_TIMEOUT(1);

// The bytes of \p packet on the wire, start byte and CRC filled in.
static std::vector<uint8_t> usbFrame(UsbPacket *packet)
{
    uint8_t *p = (uint8_t *)packet;

    packet->start_byte = USB_PACKET_START_BYTE;
    packet->crc8 = calcCrc8(p + 2, packet->data_length + 2);

    return std::vector<uint8_t>(p, p + packet->data_length + USB_PACKET_HEADER_SIZE);
}

// Throws Robotiq::common::SerialError when the port fails.
static bool usbSend(Robotiq::common::SerialPort& port, const std::vector<uint8_t>& frame)
{
    const size_t sent = port.write(frame.data(), frame.size(), USB_WRITE_TIMEOUT);
    return sent == frame.size() && port.drain(USB_WRITE_TIMEOUT);
}

static bool usbReadByte(UsbPacket *packet, unsigned int *readSoFar, uint8_t d)
{
    uint8_t *p = (uint8_t *)packet;

    // Make sure start byte is seen
    if (*readSoFar == 0 && d != USB_PACKET_START_BYTE)
        return false;

    // Buffer the byte (making sure not to overflow the packet)
    if (*readSoFar < USB_PACKET_MAX_SIZE)
        p[*readSoFar] = d;
    ++*readSoFar;

    // If length is read, stop when done
    if (*readSoFar > 3 && *readSoFar >= (unsigned)packet->data_length + USB_PACKET_HEADER_SIZE)
    {
        *readSoFar = 0;

        // If CRC is ok, we have a new packet!  Return it.
        if (packet->crc8 == calcCrc8(p + 2, packet->data_length + 2))
            return true;

        // If CRC is not ok, find the next start byte and shift the packet back
        // in hopes of getting back in sync
        for (unsigned int i = 1; i < (unsigned)packet->data_length + USB_PACKET_HEADER_SIZE; ++i)
            if (p[i] == USB_PACKET_START_BYTE)
            {
                memmove(p, p + i, packet->data_length + USB_PACKET_HEADER_SIZE - i);
                *readSoFar = packet->data_length + USB_PACKET_HEADER_SIZE - i;
                break;
            }
    }

    return false;
}

static inline uint16_t parseBigEndian2(uint8_t *data)
{
    return (uint16_t)data[0] << 8 | data[1];
}

static uint8_t extractUint16(uint16_t *to, uint16_t toCount, uint8_t *data, unsigned int size)
{
    unsigned int cur;

    // Extract 16-bit values.  If not enough data, extract as much data as available
    for (cur = 0; 2 * cur + 1 < size && cur < toCount; ++cur)
        to[cur] = parseBigEndian2(&data[2 * cur]);

    // Return number of bytes read
    return cur * 2;
}

static inline uint64_t parseBigEndian8(uint8_t *data)
{
    return  (uint64_t)data[0] << 56 | (uint64_t)data[1] << 48
          | (uint64_t)data[2] << 40 | (uint64_t)data[3] << 32
          | (uint64_t)data[4] << 24 | (uint64_t)data[5] << 16
          | (uint64_t)data[6] << 8  | (uint64_t)data[7];
}

static unsigned int extractUint64(uint64_t *to, unsigned int toCount, uint8_t *data, unsigned int size)
{
    unsigned int cur;

    for (cur = 0; 8 * cur + 7 < size && cur < toCount; ++cur)
        to[cur] = parseBigEndian8(&data[8 * cur]);

    return cur * 8;
}

static void parseSensors(UsbPacket *packet, Fingers *fingers)
{
    for (unsigned int i = 0; i < packet->data_length;)
    {
        uint8_t sensorType = packet->data[i] & 0xF0;
        uint8_t f = packet->data[i] >> 2 & 0x03;
        ++i;

        if (f >= FINGER_COUNT)
            continue;

        uint8_t *sensorData = packet->data + i;
        unsigned int sensorDataBytes = packet->data_length - i;

        switch (sensorType)
        {
        case USB_SENSOR_TYPE_DYNAMIC_TACTILE:
            i += extractUint16((uint16_t *)fingers->finger[f].dynamicTactile,
                             FINGER_DYNAMIC_TACTILE_COUNT, sensorData, sensorDataBytes);
            fingers->finger[f].newDataAvailable = true;
            break;
        case USB_SENSOR_TYPE_STATIC_TACTILE:
            i += extractUint16(fingers->finger[f].staticTactile,
                             FINGER_STATIC_TACTILE_COUNT, sensorData, sensorDataBytes);
            fingers->finger[f].newDataAvailable = true;
            break;
        case USB_SENSOR_TYPE_ACCELEROMETER:
            i += extractUint16((uint16_t *)fingers->finger[f].accelerometer,
                             3, sensorData, sensorDataBytes);
            fingers->finger[f].newDataAvailable = true;
            break;
        case USB_SENSOR_TYPE_GYROSCOPE:
            i += extractUint16((uint16_t *)fingers->finger[f].gyroscope,
                             3, sensorData, sensorDataBytes);
            fingers->finger[f].newDataAvailable = true;
            break;
        case USB_SENSOR_TYPE_TEMPERATURE:
            i += extractUint16((uint16_t *)&fingers->finger[f].temperature,
                             1, sensorData, sensorDataBytes);
            fingers->finger[f].newDataAvailable = true;
            break;
        case USB_SENSOR_TYPE_TIMESTAMP:
            i += extractUint64(&fingers->finger[f].timestamp,
                             1, sensorData, sensorDataBytes);
            fingers->finger[f].newDataAvailable = true;
            break;
        default:
            // Unknown sensor type — consume only the type byte (already done)
            // and continue, so forward-compatible additions don't kill the parse.
            break;
        }
    }
}

// ============================================================================
// RobotiqTactileSensor Implementation
// ============================================================================

RobotiqTactileSensor::RobotiqTactileSensor(const char* portName,
                                           void (*dataCallback)(const Fingers&),
                                           unsigned int period_ms)
    : m_running(false)
    , m_connected(false)
    , m_dataCallback(dataCallback)
    , m_period_ms(period_ms)
    , m_portName(portName)
    , m_receiveBuffer(1024)
    , m_dataRate(0)
    , m_calculatingBaseline(false)
    , m_versionReady(false)
{
    // Symlinks (udev aliases such as /dev/rq_tsf85_0) are followed by the OS.
    std::unique_ptr<Robotiq::common::SerialPort> port(new Robotiq::common::SerialPort());
    try
    {
        port->open(portName, USB_BAUDRATE);
    }
    catch (const Robotiq::common::SerialError& error)
    {
        m_lastError = "Failed to open serial port ";
        m_lastError += portName;
        m_lastError += ": ";
        m_lastError += error.what();
        return;
    }
    m_port = std::move(port);

    m_connected = true;
    m_running = true;

    // Start data collection thread
    m_thread = std::thread(&RobotiqTactileSensor::threadLoop, this);
}

RobotiqTactileSensor::~RobotiqTactileSensor()
{
    // Signal thread to stop
    m_running = false;

    // Wait for thread to finish
    if (m_thread.joinable())
        m_thread.join();

    // m_port closes the serial port as it goes.
}

bool RobotiqTactileSensor::isConnected() const
{
    return m_connected;
}

const char* RobotiqTactileSensor::getLastError() const
{
    return m_lastError.c_str();
}

uint64_t RobotiqTactileSensor::getDataRate() const
{
    return m_dataRate;
}

void RobotiqTactileSensor::stop()
{
    m_running = false;
}

void RobotiqTactileSensor::start()
{
    if (!m_running && m_connected)
    {
        m_running = true;
        if (!m_thread.joinable())
            m_thread = std::thread(&RobotiqTactileSensor::threadLoop, this);
    }
}

bool RobotiqTactileSensor::getFirmwareVersion(std::string& version, unsigned int timeout_ms)
{
    if (!m_connected)
    {
        m_lastError = "Cannot query firmware version: sensor not connected";
        return false;
    }

    // The read thread sends the request and collects the reply.
    if (!m_running)
    {
        m_lastError = "Cannot query firmware version: data collection is stopped";
        return false;
    }

    // Reset the handoff slot before sending the request
    {
        std::lock_guard<std::mutex> lk(m_versionMutex);
        m_versionReady = false;
        m_firmwareVersion.clear();
    }

    // Send GET_VERSION command (no payload)
    UsbPacket pkt;
    pkt.command = USB_COMMAND_GET_VERSION;
    pkt.data_length = 0;
    queueFrame(usbFrame(&pkt));

    // Wait for the read thread to populate the version
    std::unique_lock<std::mutex> lk(m_versionMutex);
    if (!m_versionCv.wait_for(lk,
                              std::chrono::milliseconds(timeout_ms),
                              [this] { return m_versionReady; }))
    {
        m_lastError = "Timeout waiting for firmware version reply";
        return false;
    }

    version = m_firmwareVersion;
    return true;
}

bool RobotiqTactileSensor::findBaseline(Fingers& baseline)
{
    if (!m_connected)
    {
        m_lastError = "Cannot calculate baseline: sensor not connected";
        return false;
    }

    // Initialize baseline to zero
    memset(&baseline, 0, sizeof(Fingers));

    // Reserve space for 1000 samples
    m_baselineMutex.lock();
    m_baselineSamples.clear();
    m_baselineSamples.reserve(1000);
    m_calculatingBaseline = true;
    m_baselineMutex.unlock();

    // Give thread time to start collecting samples
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Wait for 1000 samples to be collected
    // The threadLoop will fill m_baselineSamples when m_calculatingBaseline is true
    size_t sampleCount = 0;
    size_t lastCount = 0;
    int timeout_counter = 0;
    const int MAX_TIMEOUT = 500; // 5 seconds max (500 * 10ms)

    while (sampleCount < 1000)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Check sample count within mutex (thread-safe)
        m_baselineMutex.lock();
        sampleCount = m_baselineSamples.size();
        m_baselineMutex.unlock();

        // Debug output every 100ms to show progress
        if (timeout_counter % 10 == 0 && sampleCount != lastCount)
        {
            // Don't print anything yet, just track
            lastCount = sampleCount;
        }

        // Check if still connected
        if (!m_connected)
        {
            m_calculatingBaseline = false;
            m_lastError = "Connection lost during baseline calculation";
            return false;
        }

        // Timeout protection - but give more detail
        timeout_counter++;
        if (timeout_counter >= MAX_TIMEOUT)
        {
            m_calculatingBaseline = false;
            char errBuf[256];
            snprintf(errBuf, sizeof(errBuf),
                    "Timeout during baseline calculation (collected %zu/1000 samples)",
                    sampleCount);
            m_lastError = errBuf;
            return false;
        }
    }

    // Calculate averages
    m_baselineMutex.lock();
    m_calculatingBaseline = false;

    // Accumulate values for each taxel
    uint32_t accumulator[FINGER_COUNT][FINGER_STATIC_TACTILE_COUNT] = {0};

    for (const auto& sample : m_baselineSamples)
    {
        for (int f = 0; f < FINGER_COUNT; f++)
        {
            for (int i = 0; i < FINGER_STATIC_TACTILE_COUNT; i++)
            {
                accumulator[f][i] += sample.finger[f].staticTactile[i];
            }
        }
    }

    // Calculate averages and store in baseline
    for (int f = 0; f < FINGER_COUNT; f++)
    {
        for (int i = 0; i < FINGER_STATIC_TACTILE_COUNT; i++)
        {
            baseline.finger[f].baseline[i] = accumulator[f][i] / 1000;
        }
    }

    m_baselineSamples.clear();
    m_baselineMutex.unlock();

    return true;
}

void RobotiqTactileSensor::threadLoop()
{
    try
    {
        collectData();
    }
    catch (const Robotiq::common::SerialError&)
    {
        // The sensor is gone (unplugged, port error): stop collecting and
        // report it through isConnected(). m_lastError stays the user
        // thread's, which reads it without synchronization.
        m_connected = false;
        m_running = false;
    }
}

void RobotiqTactileSensor::collectData()
{
    UsbPacket send;
    UsbPacket recv;
    unsigned int recvSoFar = 0;

    // Timer for timestamps
    auto startTime = std::chrono::steady_clock::now();

    // Timer and info used to calculate data rate
    auto dataRateTime = std::chrono::steady_clock::now();
    unsigned int receivedBytes = 0;

    // Gathered data
    Fingers fingers = {0};

    // Send auto-send message
    send.command = USB_COMMAND_AUTOSEND_SYNC_SENSORS;
    send.data_length = 1;
    send.data[0] = m_period_ms;
    usbSend(*m_port, usbFrame(&send));

    uint8_t* buffer = reinterpret_cast<uint8_t*>(m_receiveBuffer.data());
    while (m_running)
    {
        sendPendingFrames();

        // Wait for at least 1 byte with 1ms timeout (matches Qt's waitForReadyRead)
        if (m_port->read(buffer, 1, USB_READ_TIMEOUT) != 1)
            continue;  // Timeout

        // Then take whatever else is already buffered, up to the buffer's size;
        // the rest stays queued in the OS for the next pass.
        int64_t available = 1 + static_cast<int64_t>(m_port->read(
            buffer + 1, m_receiveBuffer.size() - 1, std::chrono::milliseconds(0)));

        // Show progress
        receivedBytes += available;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - dataRateTime).count();
        if (elapsed > 200)
        {
            m_dataRate = (uint64_t)receivedBytes * 1000 / elapsed;
            receivedBytes = 0;
            dataRateTime = now;
        }

        // Parse packets and store sensor values
        for (int64_t i = 0; i < available; ++i)
        {
            if (usbReadByte(&recv, &recvSoFar, m_receiveBuffer[i]))
            {
                // Firmware-version reply: hand off to any waiting getFirmwareVersion() call
                if (recv.command == USB_COMMAND_GET_VERSION)
                {
                    {
                        std::lock_guard<std::mutex> lk(m_versionMutex);
                        m_firmwareVersion.assign(
                            reinterpret_cast<const char *>(recv.data),
                            recv.data_length);
                        m_versionReady = true;
                    }
                    m_versionCv.notify_all();
                    continue;
                }

                // Any other non-sensor reply is ignored to avoid mis-parsing
                if (recv.command != USB_COMMAND_AUTOSEND_SYNC_SENSORS &&
                    recv.command != USB_COMMAND_AUTOSEND_ASYNC_SENSORS &&
                    recv.command != USB_COMMAND_READ_SENSORS)
                {
                    continue;
                }

                parseSensors(&recv, &fingers);

                // A packet may carry data for one or both fingers. Fire the
                // callback once per packet whenever any finger was updated.
                bool anyNew = false;
                for (int f = 0; f < FINGER_COUNT; f++)
                    if (fingers.finger[f].newDataAvailable)
                    {
                        anyNew = true;
                        break;
                    }

                if (anyNew)
                {
                    auto timestamp = std::chrono::steady_clock::now();
                    fingers.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                        timestamp - startTime).count();

                    // If calculating baseline, store sample instead of calling callback
                    if (m_calculatingBaseline)
                    {
                        m_baselineMutex.lock();
                        if (m_baselineSamples.size() < 1000)
                        {
                            m_baselineSamples.push_back(fingers);
                        }
                        m_baselineMutex.unlock();
                    }
                    else if (m_dataCallback)
                    {
                        m_dataCallback(fingers);
                    }

                    // Clear flags so the next packet starts fresh
                    for (int f = 0; f < FINGER_COUNT; f++)
                        fingers.finger[f].newDataAvailable = false;
                }
            }
        }
    }

    // Stop auto-send message
    send.command = USB_COMMAND_AUTOSEND_SYNC_SENSORS;
    send.data_length = 1;
    send.data[0] = 0;
    usbSend(*m_port, usbFrame(&send));
}

void RobotiqTactileSensor::queueFrame(const std::vector<uint8_t>& frame)
{
    std::lock_guard<std::mutex> lk(m_pendingFramesMutex);
    m_pendingFrames.push_back(frame);
}

void RobotiqTactileSensor::sendPendingFrames()
{
    std::vector<std::vector<uint8_t> > frames;
    {
        std::lock_guard<std::mutex> lk(m_pendingFramesMutex);
        frames.swap(m_pendingFrames);
    }
    for (size_t i = 0; i < frames.size(); ++i)
        usbSend(*m_port, frames[i]);
}
