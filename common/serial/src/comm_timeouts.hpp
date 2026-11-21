// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// Without <windows.h>, so it builds on every platform.

#pragma once

#include <cstdint>

namespace Robotiq::common::detail {
// MAXDWORD.
constexpr uint32_t kMaxDword = 0xFFFFFFFF;

struct CommTimeouts
{
   uint32_t readInterval = 0;
   uint32_t readMultiplier = 0;
   uint32_t readConstant = 0;
   uint32_t writeConstant = 0;
};

constexpr bool operator==(const CommTimeouts& a, const CommTimeouts& b)
{
   return a.readInterval == b.readInterval && a.readMultiplier == b.readMultiplier && a.readConstant == b.readConstant
       && a.writeConstant == b.writeConstant;
}

// The timeouts for a read of up to \p readMs and a write of up to
// \p writeMs. A zero \p readMs returns what is buffered at once. Otherwise
// the read returns once the requested count arrives, or, with
// \p allowPartialRead, once any byte does.
constexpr CommTimeouts commTimeouts(bool allowPartialRead, uint32_t readMs, uint32_t writeMs)
{
   CommTimeouts timeouts;
   if(readMs == 0)
   {
      timeouts.readInterval = kMaxDword;
   }
   else if(allowPartialRead)
   {
      timeouts.readInterval = kMaxDword;
      timeouts.readMultiplier = kMaxDword;
      timeouts.readConstant = readMs;
   }
   else
   {
      timeouts.readConstant = readMs;
   }
   timeouts.writeConstant = writeMs;
   return timeouts;
}
} // namespace Robotiq::common::detail
