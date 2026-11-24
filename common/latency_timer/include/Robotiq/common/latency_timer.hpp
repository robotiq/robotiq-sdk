// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief The Linux FTDI `latency_timer`, through sysfs. Its 16 ms kernel
//! default triples a request/reply cycle; 1 ms restores it.

#pragma once

#include <Robotiq/common/abi.hpp>

#ifdef __linux__

#include <string>

namespace Robotiq::common {
inline namespace ROBOTIQ_COMMON_ABI {
// Empty when \p port is not an FTDI adapter. Symlinks are followed.
[[nodiscard]] std::string latencyTimerPath(const std::string& port,
                                           const std::string& sysfsDevices = "/sys/bus/usb-serial/devices");

// False when the attribute is not writable and does not already hold
// \p milliseconds.
[[nodiscard]] bool applyLatencyTimer(const std::string& path, int milliseconds);
} // namespace ROBOTIQ_COMMON_ABI
} // namespace Robotiq::common

#endif
