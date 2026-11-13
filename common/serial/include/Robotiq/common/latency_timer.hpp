// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief The Linux FTDI `latency_timer`, reached through sysfs.
//! The kernel default of 16 ms holds back every read until the adapter's
//! buffer fills or the timer expires, which on a request/reply link
//! triples the cycle time; 1 ms restores it. Only Linux exposes it; the
//! Windows and macOS drivers keep it in their own settings.

#pragma once

#ifdef __linux__

#include <string>

namespace Robotiq::common {
// The latency_timer attribute behind \p port, or empty when the port is
// not an FTDI adapter (ptys, CDC-ACM devices, other USB chips). Symlinks
// (/dev/serial/by-id, udev aliases, socat links) are followed to the
// device node, whose name is the sysfs entry.
[[nodiscard]] std::string latencyTimerPath(const std::string& port,
                                           const std::string& sysfsDevices = "/sys/bus/usb-serial/devices");

// Best-effort write of \p milliseconds to the attribute at \p path; false
// when it could not be applied (sysfs is root-owned unless a udev rule
// says otherwise). A value already in place counts as applied.
[[nodiscard]] bool applyLatencyTimer(const std::string& path, int milliseconds);
} // namespace Robotiq::common

#endif
