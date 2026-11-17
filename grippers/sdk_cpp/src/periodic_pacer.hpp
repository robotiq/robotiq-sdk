// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Internal: the pacer behind a Gripper built with an exchangePeriod.

#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <utility>

#include <Robotiq/gripper/exchange_pacer.hpp>
#include <Robotiq/gripper/platform.hpp>

namespace Robotiq::detail {

class PeriodicPacer : public ExchangePacer
{
public:
   PeriodicPacer(std::chrono::microseconds period, std::shared_ptr<Platform> platform)
      : _period(period)
      , _platform(std::move(platform))
   {
   }

   void waitForNextCycle() noexcept override
   {
      if(!_nextCycle)
      {
         _nextCycle = std::chrono::steady_clock::now();
         return;
      }
      // Overrun cycles (e.g. timeouts during a fault) must not
      // accumulate a backlog that bursts exchanges on recovery.
      _nextCycle = std::max(*_nextCycle + _period, std::chrono::steady_clock::now());
      _platform->sleepUntil(*_nextCycle);
   }

   // The sleep lasts at most one period.
   void wake() noexcept override {}

private:
   std::chrono::microseconds _period;
   std::shared_ptr<Platform> _platform;
   std::optional<std::chrono::steady_clock::time_point> _nextCycle;
};

} // namespace Robotiq::detail
