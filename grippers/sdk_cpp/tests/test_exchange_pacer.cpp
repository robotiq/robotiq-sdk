// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/driver_exception.hpp>
#include <Robotiq/gripper/exchange_pacer.hpp>
#include <Robotiq/gripper/platform.hpp>
#include <Robotiq/gripper/stamped_exchange.hpp>

#include "fake_gripper_fixture.hpp"
#include "gripper_test_helpers.hpp"

namespace Robotiq::test {
namespace {
constexpr std::chrono::seconds kWait{2};

//! Stands in for an external tick: each release() lets one exchange start.
class ManualPacer : public ExchangePacer
{
public:
   void waitForNextCycle() noexcept override
   {
      std::unique_lock<std::mutex> lock(_mutex);
      ++_waiting;
      _changed.notify_all();
      _changed.wait(lock, [this] { return _released > 0 || _woken; });
      --_waiting;
      if(_released > 0)
      {
         --_released;
      }
   }

   void wake() noexcept override
   {
      const std::lock_guard<std::mutex> lock(_mutex);
      _woken = true;
      _changed.notify_all();
   }

   void release(uint64_t cycles = 1)
   {
      const std::lock_guard<std::mutex> lock(_mutex);
      _released += cycles;
      _changed.notify_all();
   }

   bool waitUntilBlocked(std::chrono::milliseconds timeout)
   {
      std::unique_lock<std::mutex> lock(_mutex);
      return _changed.wait_for(lock, timeout, [this] { return _waiting > 0 && _released == 0; });
   }

   bool woken()
   {
      const std::lock_guard<std::mutex> lock(_mutex);
      return _woken;
   }

private:
   std::mutex _mutex;
   std::condition_variable _changed;
   uint64_t _released = 0;
   int _waiting = 0;
   bool _woken = false;
};

std::unique_ptr<Gripper> makePacedGripper(InstrumentedFakeGripperServer& fakeServer,
                                          std::shared_ptr<ExchangePacer> pacer)
{
   return std::make_unique<Gripper>(std::make_unique<fake::GripperSerial>(fakeServer.server),
                                    kSlaveAddress,
                                    std::move(pacer),
                                    makeDefaultPlatform(),
                                    std::make_shared<NullLogger>());
}

} // namespace

TEST(TestExchangePacer, no_exchange_starts_before_the_pacer_releases_one)
{
   InstrumentedFakeGripperServer fakeServer;
   const auto pacer = std::make_shared<ManualPacer>();
   const auto gripper = makePacedGripper(fakeServer, pacer);

   ASSERT_TRUE(pacer->waitUntilBlocked(kWait));
   EXPECT_EQ(gripper->getMostRecentStampedExchange().metadata.exchangeCount, 0U);
}

TEST(TestExchangePacer, each_release_runs_exactly_one_exchange)
{
   InstrumentedFakeGripperServer fakeServer;
   const auto pacer = std::make_shared<ManualPacer>();
   const auto gripper = makePacedGripper(fakeServer, pacer);

   pacer->release(3);
   const std::optional<StampedExchange> third = gripper->waitForExchangeCount(3, kWait);
   ASSERT_TRUE(third.has_value());
   ASSERT_TRUE(pacer->waitUntilBlocked(kWait));
   EXPECT_EQ(gripper->getMostRecentStampedExchange().metadata.exchangeCount, 3U);
}

TEST(TestExchangePacer, destruction_wakes_a_pacer_that_is_still_waiting)
{
   InstrumentedFakeGripperServer fakeServer;
   const auto pacer = std::make_shared<ManualPacer>();
   auto gripper = makePacedGripper(fakeServer, pacer);
   ASSERT_TRUE(pacer->waitUntilBlocked(kWait));

   gripper.reset();

   EXPECT_TRUE(pacer->woken());
}

TEST(TestExchangePacer, a_failed_construction_leaves_the_pacer_reusable)
{
   InstrumentedFakeGripperServer fakeServer;
   const auto pacer = std::make_shared<ManualPacer>();
   auto deadLink = std::make_unique<WriteFailingSerial>(fakeServer.server);
   deadLink->failing.store(true);

   EXPECT_THROW(
      Gripper(std::move(deadLink), kSlaveAddress, pacer, makeDefaultPlatform(), std::make_shared<NullLogger>()),
      DriverException);

   EXPECT_FALSE(pacer->woken());
}

TEST(TestExchangePacer, a_null_pacer_is_rejected)
{
   InstrumentedFakeGripperServer fakeServer;
   EXPECT_THROW(makePacedGripper(fakeServer, nullptr), DriverException);
}

} // namespace Robotiq::test
