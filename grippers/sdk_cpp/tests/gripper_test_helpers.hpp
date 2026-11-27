// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include "fake/gripper_serial.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/connection_state.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/platform.hpp>
#include <Robotiq/gripper/serial_io_exception.hpp>

#include "fake_gripper_fixture.hpp"
#include "test_utils.hpp"

namespace Robotiq::test {

inline constexpr std::chrono::milliseconds kFastPeriod{1};
//! Slow enough that the next cycle is always still ahead when a wait
//! starts, so what a sync test observes is a real wait.
inline constexpr std::chrono::milliseconds kSyncPeriod{20};
//! Long enough for any event a test waits on to happen, on a loaded machine
//! too.
inline constexpr std::chrono::seconds kWait{2};

//! A Gripper exchanging with \p fakeServer. Returned as a prvalue: Gripper is
//! neither copyable nor movable, and C++17's guaranteed elision is what lets
//! a factory hand one back by value anyway.
inline Gripper makeGripper(InstrumentedFakeGripperServer& fakeServer,
                           std::chrono::milliseconds period,
                           std::shared_ptr<Platform> platform = makeDefaultPlatform())
{
   return Gripper(std::make_unique<fake::GripperSerial>(fakeServer.server),
                  kSlaveAddress,
                  period,
                  std::move(platform),
                  std::make_shared<NullLogger>());
}

//! GripperSerial whose writes fail while failing is set — a link
//! that starts healthy, drops out, and comes back. Failing on write
//! keeps the fake's reply streams free of stale responses.
class WriteFailingSerial : public fake::GripperSerial
{
public:
   using fake::GripperSerial::GripperSerial;

   void write(const std::vector<uint8_t>& data) override
   {
      if(failing.load())
      {
         throw SerialIOException("injected wire failure");
      }
      GripperSerial::write(data);
   }

   std::atomic<bool> failing{false};
};

//! A gripper whose link has already failed for good: no exchange will ever
//! complete again, so nothing but the stop can end a wait on it.
inline std::unique_ptr<Gripper> makeStalledGripper(InstrumentedFakeGripperServer& fakeServer)
{
   auto serial = std::make_unique<WriteFailingSerial>(fakeServer.server);
   WriteFailingSerial& link = *serial;
   auto gripper = std::make_unique<Gripper>(std::move(serial),
                                            kSlaveAddress,
                                            kFastPeriod,
                                            makeDefaultPlatform(),
                                            std::make_shared<NullLogger>());
   link.failing.store(true);
   EXPECT_TRUE(pollFor([&] { return gripper->connectionState() == ConnectionState::Faulted; }, kWait));
   return gripper;
}

} // namespace Robotiq::test
