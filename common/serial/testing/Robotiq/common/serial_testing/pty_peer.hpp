// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief A pseudo-terminal standing in for a serial device in tests (POSIX).
//! The slave is the port under test; the master is the device's end.

#pragma once

#ifndef _WIN32

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace Robotiq::common::serial_testing {
class PtyPeer
{
public:
   PtyPeer()
   {
      _master = ::posix_openpt(O_RDWR | O_NOCTTY);
      if(_master < 0 || ::grantpt(_master) != 0 || ::unlockpt(_master) != 0)
      {
         return;
      }
      const char* name = ::ptsname(_master);
      if(name != nullptr)
      {
         _slavePath = name;
      }
   }

   ~PtyPeer() { hangUp(); }

   PtyPeer(const PtyPeer&) = delete;
   PtyPeer& operator=(const PtyPeer&) = delete;

   [[nodiscard]] bool valid() const { return _master >= 0 && !_slavePath.empty(); }

   [[nodiscard]] const std::string& slavePath() const { return _slavePath; }

   // As an unplugged device would.
   void hangUp()
   {
      if(_master >= 0)
      {
         ::close(_master);
         _master = -1;
      }
   }

   [[nodiscard]] bool send(const std::vector<uint8_t>& data) const
   {
      return ::write(_master, data.data(), data.size()) == static_cast<ssize_t>(data.size());
   }

   // Fewer than \p size bytes when \p timeout passes first.
   [[nodiscard]] std::vector<uint8_t> receive(size_t size,
                                              std::chrono::milliseconds timeout = std::chrono::seconds(10)) const
   {
      std::vector<uint8_t> data(size);
      size_t received = 0;
      const auto deadline = std::chrono::steady_clock::now() + timeout;
      while(received < size && std::chrono::steady_clock::now() < deadline)
      {
         struct pollfd descriptor = {};
         descriptor.fd = _master;
         descriptor.events = POLLIN;
         if(::poll(&descriptor, 1, 10) <= 0)
         {
            continue;
         }
         const ssize_t count = ::read(_master, data.data() + received, size - received);
         if(count > 0)
         {
            received += static_cast<size_t>(count);
         }
         else if(count < 0 && errno != EAGAIN && errno != EINTR)
         {
            break;
         }
      }
      data.resize(received);
      return data;
   }

private:
   int _master = -1;
   std::string _slavePath;
};
} // namespace Robotiq::common::serial_testing

#endif
