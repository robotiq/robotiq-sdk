// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

namespace Robotiq {

//! \ingroup platform
//! \brief Decides when each exchange starts, in place of a fixed period
//! (e.g. on a firmware's control tick).
//!
//! The exchange thread calls waitForNextCycle() before every exchange.
//! To keep a fixed phase, return on absolute tick indices, so an overrun
//! skips a slot instead of shifting the next ones.
//!
//! One pacer serves one Gripper at a time. wake() is called once, from the
//! destroying thread, only if the exchange thread started; a woken pacer
//! must not be reused.
class ExchangePacer
{
public:
   virtual ~ExchangePacer() = default;

   //! Block until the next exchange should start. Must not throw.
   virtual void waitForNextCycle() noexcept = 0;

   //! Make the current and every later waitForNextCycle() return promptly.
   virtual void wake() noexcept = 0;
};

} // namespace Robotiq
