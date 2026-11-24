// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <Robotiq/common/abi.hpp>

#include <algorithm>
#include <chrono>

namespace Robotiq::common {
inline namespace ROBOTIQ_COMMON_ABI {
namespace detail {
constexpr std::chrono::nanoseconds kMaxDrainPoll = std::chrono::milliseconds(1);

// The queue length is only a hint: cdc-acm reports its whole write buffer
// while a transfer is in flight.
constexpr std::chrono::nanoseconds drainWait(int queued,
                                             std::chrono::nanoseconds byteTime,
                                             std::chrono::nanoseconds remaining)
{
   return std::min({byteTime * queued, kMaxDrainPoll, remaining});
}

// True once \p queued() reports an empty queue, or fails (negative); false
// when \p timeout passes first. A zero \p timeout waits without bound.
template <typename Queued, typename Now, typename Sleep>
bool waitForEmptyQueue(Queued queued,
                       std::chrono::nanoseconds byteTime,
                       std::chrono::milliseconds timeout,
                       Now now,
                       Sleep sleep)
{
   const bool unbounded = timeout == std::chrono::milliseconds::zero();
   const auto deadline = now() + timeout;
   for(int count = queued(); count > 0; count = queued())
   {
      const auto remaining = unbounded ? std::chrono::nanoseconds::max() : deadline - now();
      if(remaining <= std::chrono::nanoseconds::zero())
      {
         return false;
      }
      sleep(drainWait(count, byteTime, remaining));
   }
   return true;
}
} // namespace detail
} // namespace ROBOTIQ_COMMON_ABI
} // namespace Robotiq::common
