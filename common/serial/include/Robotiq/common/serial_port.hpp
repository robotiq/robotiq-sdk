// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief The OS serial port the Robotiq SDKs share, internal to them.
//! Failures throw SerialError. Not thread-safe.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Robotiq::common {
class SerialPort
{
public:
   SerialPort() = default;
   ~SerialPort();

   SerialPort(const SerialPort&) = delete;
   SerialPort& operator=(const SerialPort&) = delete;

   // Open \p port at \p baudrate, 8N1, no flow control, raw.
   void open(const std::string& port, uint32_t baudrate);

   // Release the handle. Safe to call when already closed.
   void close();

   // Bytes actually read — fewer than size when the deadline passed first. A
   // zero timeout takes only what is buffered.
   [[nodiscard]] size_t read(uint8_t* data, size_t size, std::chrono::milliseconds timeout);

   // Bytes actually written; a short count means the write timed out. A
   // zero timeout never times out.
   [[nodiscard]] size_t write(const uint8_t* data, size_t size, std::chrono::milliseconds timeout);

   // Block until the bytes written have left; false when the timeout passed
   // first. It bounds the driver's output queue, not the final tcdrain(),
   // and Windows leaves the whole wait to its driver. A zero timeout never
   // times out.
   [[nodiscard]] bool drain(std::chrono::milliseconds timeout);

private:
#ifdef _WIN32
   // The COMMTIMEOUTS the handle currently carries, so a steady exchange
   // does not re-enter the driver on every read; -1 until open() applies
   // the first pair. Both fields live in one struct, hence one cache.
   void setTimeouts(int64_t readMs, int64_t writeMs);

   void* _handle = nullptr; // HANDLE, kept untyped to spare callers <windows.h>
   int64_t _readTimeoutMs = -1;
   int64_t _writeTimeoutMs = -1;
#else
   int _fd = -1;
   uint32_t _baudrate = 0;
#endif
};
} // namespace Robotiq::common
