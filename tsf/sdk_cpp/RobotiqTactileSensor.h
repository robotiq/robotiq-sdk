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

#ifndef ROBOTIQ_TACTILE_SENSOR_H
#define ROBOTIQ_TACTILE_SENSOR_H

#include "finger_data.h"
#include <thread>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>
#include <stdint.h>

// The serial transport the Robotiq SDKs share. Internal: it is compiled into
// this library and never included from its public headers.
namespace Robotiq {
namespace common {
class SerialPort;
}
}

/**
 * @class RobotiqTactileSensor
 * @brief Cross-platform C++ SDK for Robotiq Tactile Sensor
 *
 * This class provides a threaded interface to communicate with Robotiq tactile sensors
 * via USB serial port. Data is collected in a background thread at configurable rates
 * and delivered via callback function.
 */
class RobotiqTactileSensor
{
public:
    /**
     * @brief Constructor - Opens serial port and starts data collection
     * @param portName Serial port path (e.g., "/dev/ttyUSB0" on Linux, "COM3" on Windows)
     * @param dataCallback Function pointer called when new sensor data arrives
     * @param period_ms Sampling period in milliseconds (default: 1ms for 1KHz)
     */
    RobotiqTactileSensor(const char* portName,
                         void (*dataCallback)(const Fingers&),
                         unsigned int period_ms = 1);

    /**
     * @brief Destructor - Stops thread and closes port
     */
    ~RobotiqTactileSensor();

    /**
     * @brief Check if port opened successfully
     * @return true if connected, false otherwise
     */
    bool isConnected() const;

    /**
     * @brief Get last error message
     * @return Error description string (empty if no error)
     */
    const char* getLastError() const;

    /**
     * @brief Get current data rate in bytes/sec
     * @return Data rate
     */
    uint64_t getDataRate() const;

    /**
     * @brief Manually stop data collection
     */
    void stop();

    /**
     * @brief Manually start data collection (after stop)
     */
    void start();

    /**
     * @brief Query the firmware version string from the sensor
     *
     * Sends a GET_VERSION command and waits for the reply (handled by the
     * background read thread). Safe to call while data collection is running.
     *
     * @param version Output parameter receiving the ASCII firmware version
     * @param timeout_ms Maximum time to wait for a reply (default 2000 ms)
     * @return true on success, false on timeout or disconnect
     */
    bool getFirmwareVersion(std::string& version, unsigned int timeout_ms = 2000);

    /**
     * @brief Calculate baseline values by averaging 1000 samples
     *
     * This function collects 1000 samples from the sensor and calculates
     * the average value for each taxel. These baseline values are stored
     * in the Fingers structure and can be subtracted from future readings.
     *
     * @param baseline Reference to Fingers structure to store baseline values
     * @return true if baseline calculated successfully, false otherwise
     */
    bool findBaseline(Fingers& baseline);

private:
    // Internal thread function
    void threadLoop();

    // The read thread's work; throws Robotiq::common::SerialError when the
    // port fails.
    void collectData();

    // Sends the frames queued by other threads; called from the read thread.
    void sendPendingFrames();

    // Queues \p frame for the read thread to send.
    void queueFrame(const std::vector<uint8_t>& frame);

    // Serial port; null when the constructor could not open it. Once the
    // read thread runs, only that thread touches it.
    std::unique_ptr<Robotiq::common::SerialPort> m_port;

    // Threading
    std::thread m_thread;
    std::atomic<bool> m_running;
    std::atomic<bool> m_connected;

    // Callback
    void (*m_dataCallback)(const Fingers&);

    // Configuration
    unsigned int m_period_ms;
    std::string m_portName;

    // Buffers
    std::vector<char> m_receiveBuffer;

    // Error tracking
    std::string m_lastError;

    // Data rate tracking
    std::atomic<uint64_t> m_dataRate;

    // Baseline calculation
    std::atomic<bool> m_calculatingBaseline;
    std::vector<Fingers> m_baselineSamples;
    std::mutex m_baselineMutex;

    // Firmware version request/response handoff between user and read thread
    std::mutex m_versionMutex;
    std::condition_variable m_versionCv;
    std::string m_firmwareVersion;
    bool m_versionReady;

    // Frames other threads hand to the read thread, which alone touches the
    // port: the serial transport is not thread-safe.
    std::mutex m_pendingFramesMutex;
    std::vector<std::vector<uint8_t> > m_pendingFrames;

    // Prevent copying
    RobotiqTactileSensor(const RobotiqTactileSensor&) = delete;
    RobotiqTactileSensor& operator=(const RobotiqTactileSensor&) = delete;
};

#endif // ROBOTIQ_TACTILE_SENSOR_H
