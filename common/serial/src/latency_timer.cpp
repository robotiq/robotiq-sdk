// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <Robotiq/common/latency_timer.hpp>

#ifdef __linux__

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace Robotiq::common {
std::string latencyTimerPath(const std::string& port, const std::string& sysfsDevices)
{
   std::error_code error;
   const std::filesystem::path device = std::filesystem::canonical(port, error);
   if(error)
   {
      return {};
   }
   const std::filesystem::path attribute = std::filesystem::path(sysfsDevices) / device.filename() / "latency_timer";
   return std::filesystem::exists(attribute, error) ? attribute.string() : std::string();
}

bool applyLatencyTimer(const std::string& path, int milliseconds)
{
   // Read-before-write: sysfs is root-owned, so an unprivileged caller can't
   // write to it. If a privileged init (udev rule, container entrypoint) has
   // already pinned the value, skip the write — otherwise the caller would
   // report a failure about a device that's already configured.
   {
      std::ifstream in(path);
      int current = -1;
      if(in >> current && current == milliseconds)
      {
         return true;
      }
   }

   std::ofstream out(path);
   if(!out.is_open())
   {
      return false;
   }
   // sysfs rejects an out-of-range value only when the buffer reaches it.
   out << milliseconds << std::flush;
   return out.good();
}
} // namespace Robotiq::common

#endif
