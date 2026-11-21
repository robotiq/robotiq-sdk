// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <algorithm>
#include <chrono>

namespace Robotiq::common::detail {
constexpr std::chrono::nanoseconds kMaxDrainPoll = std::chrono::milliseconds(1);

// The queue length is only a hint: cdc-acm reports its whole write buffer
// while a transfer is in flight.
constexpr std::chrono::nanoseconds drainWait(int queued,
                                             std::chrono::nanoseconds byteTime,
                                             std::chrono::nanoseconds remaining)
{
   return std::min({byteTime * queued, kMaxDrainPoll, remaining});
}
} // namespace Robotiq::common::detail
