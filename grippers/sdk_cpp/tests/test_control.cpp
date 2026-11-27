// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/command.hpp>
#include <Robotiq/detail/control.hpp>
#include <Robotiq/detail/motion_outcome.hpp>
#include <Robotiq/gripper/connection_state.hpp>
#include <Robotiq/gripper/device_profile.hpp>
#include <Robotiq/gripper/fault_status.hpp>
#include <Robotiq/gripper/logger.hpp>
#include <Robotiq/gripper/platform.hpp>
#include <Robotiq/gripper/stamped_exchange.hpp>
#include <Robotiq/gripper/status.hpp>
#include <Robotiq/gripper/wait.hpp>

#include "fake/status_writer.hpp"
#include "fake_gripper_fixture.hpp"
#include "gripper_test_helpers.hpp"
#include "test_utils.hpp"

namespace Robotiq::test {
using namespace std::chrono_literals;
using namespace Robotiq::detail;
using profiles::k2F85;

namespace {
constexpr uint8_t rounded(double value)
{
   return static_cast<uint8_t>(value + 0.5);
}

constexpr double kOpenFraction = 0.4;
constexpr double kSpeedFraction = 0.2;
constexpr double kEffort = 0.6;
constexpr int kRegisterSpan = k2F85.closedPosition - k2F85.openPosition;
constexpr int kMaxRegister = 255;

constexpr double kOpening = k2F85.minOpening + kOpenFraction * (k2F85.maxOpening - k2F85.minOpening);
constexpr uint8_t kOpeningRegister = rounded(k2F85.closedPosition - kOpenFraction * kRegisterSpan);
constexpr double kSpeed = k2F85.minSpeed + kSpeedFraction * (k2F85.maxSpeed - k2F85.minSpeed);
constexpr uint8_t kSpeedRegister = rounded(kSpeedFraction * kMaxRegister);
constexpr uint8_t kEffortRegister = rounded(kEffort * kMaxRegister);

constexpr uint8_t kObjectRegister = 100;
constexpr double kObjectWidth =
   (k2F85.closedPosition - kObjectRegister) * (k2F85.maxOpening - k2F85.minOpening) / kRegisterSpan;

constexpr double kOpeningTolerance = (k2F85.maxOpening - k2F85.minOpening) / kRegisterSpan;
constexpr double kFullEffort = 1.0;
constexpr std::chrono::milliseconds kShortTimeout{300};
constexpr std::chrono::seconds kLongestOverrun{2};
constexpr std::chrono::seconds kTimeoutLongEnoughForAnExchange{1};
constexpr int kMotionLagExchanges = 5;

constexpr int kEchoLagExchanges = 500;
constexpr std::chrono::milliseconds kEchoLagFloor = kEchoLagExchanges * kFastPeriod;
constexpr std::chrono::milliseconds kTimeoutPastTheEchoLag{800};
constexpr std::chrono::milliseconds kLatestReturnWithTheEchoCounted = kTimeoutPastTheEchoLag + kEchoLagFloor / 2;

constexpr std::chrono::milliseconds kTimeoutBelowTheStartWindow{50};
constexpr std::chrono::milliseconds kStartWindowFloor{150};
constexpr std::chrono::milliseconds kAtOnce{100};

constexpr int kTimingAttempts = 5;

template <typename Motion>
std::chrono::steady_clock::duration durationOf(Motion motion)
{
   const auto start = std::chrono::steady_clock::now();
   motion();
   return std::chrono::steady_clock::now() - start;
}

// A loaded machine can stall any single run, so a limit on elapsed time
// holds when one of a few attempts stays under it.
template <typename Motion>
bool finishesWithin(std::chrono::steady_clock::duration limit, Motion motion)
{
   for(int attempt = 0; attempt < kTimingAttempts; ++attempt)
   {
      if(durationOf(motion) < limit)
      {
         return true;
      }
   }
   return false;
}
} // namespace

class TestControl : public ::testing::Test
{
protected:
   TestControl()
      : gripper(makeGripper(fakeServer, kFastPeriod))
   {
      activate(gripper);
   }

   void latchFault(GripperFault fault)
   {
      fakeServer.model.setFault(fault);
      ASSERT_TRUE(waitFor(
         gripper,
         [fault](const StampedExchange& exchange) { return exchange.status.faultStatus.gripperFault() == fault; },
         kWait));
   }

   InstrumentedFakeGripperServer fakeServer;
   Gripper gripper;
};

TEST_F(TestControl, move_to_reaches_the_requested_opening)
{
   const MotionResult result = moveTo(gripper, kOpening, k2F85, kSpeed, kEffort);

   EXPECT_EQ(result.outcome, MotionOutcome::Reached);
   EXPECT_EQ(result.finalStatus.position, kOpeningRegister);
   EXPECT_EQ(result.finalStatus.gripperStatus.objectDetection(), ObjectDetection::AtRequestedPosition);

   const GripperCommand sent = gripper.getCommand();
   EXPECT_TRUE(sent.action.get(ActionRequestBit::Activate));
   EXPECT_TRUE(sent.action.get(ActionRequestBit::GoTo));
   EXPECT_EQ(sent.positionRequest, kOpeningRegister);
   EXPECT_EQ(sent.speed, kSpeedRegister);
   EXPECT_EQ(sent.force, kEffortRegister);
}

TEST_F(TestControl, move_to_stops_on_an_object_before_the_requested_opening)
{
   fakeServer.model.setObjectAt(kObjectRegister);

   const MotionResult result = moveTo(gripper, k2F85.minOpening, k2F85, kSpeed, kEffort);

   EXPECT_EQ(result.outcome, MotionOutcome::StoppedOnObject);
   EXPECT_EQ(result.finalStatus.position, kObjectRegister);
   EXPECT_EQ(result.finalStatus.gripperStatus.objectDetection(), ObjectDetection::DetectedWhileClosing);
}

TEST_F(TestControl, move_to_stops_on_an_obstacle_while_opening)
{
   ASSERT_EQ(close(gripper, k2F85, kSpeed, kEffort).outcome, MotionOutcome::Reached);
   fakeServer.model.setOpeningBlockedAt(kObjectRegister);

   const MotionResult result = moveTo(gripper, k2F85.maxOpening, k2F85, kSpeed, kEffort);

   EXPECT_EQ(result.outcome, MotionOutcome::StoppedOnObject);
   EXPECT_EQ(result.finalStatus.position, kObjectRegister);
   EXPECT_EQ(result.finalStatus.gripperStatus.objectDetection(), ObjectDetection::DetectedWhileOpening);
}

TEST_F(TestControl, move_to_waits_for_a_motion_that_is_slow_to_start)
{
   ASSERT_EQ(close(gripper, k2F85, kSpeed, kEffort).outcome, MotionOutcome::Reached);
   fakeServer.model.setMotionLag(kMotionLagExchanges);

   const MotionResult result = moveTo(gripper, kOpening, k2F85, kSpeed, kEffort);

   EXPECT_EQ(result.outcome, MotionOutcome::Reached);
   EXPECT_EQ(result.finalStatus.position, kOpeningRegister);
}

TEST_F(TestControl, move_to_again_on_a_held_object_reports_the_same_stop)
{
   fakeServer.model.setObjectAt(kObjectRegister);
   ASSERT_EQ(close(gripper, k2F85, kSpeed, kEffort).outcome, MotionOutcome::StoppedOnObject);

   const MotionResult result = close(gripper, k2F85, kSpeed, kFullEffort);

   EXPECT_EQ(result.outcome, MotionOutcome::StoppedOnObject);
   EXPECT_EQ(result.finalStatus.position, kObjectRegister);
   const GripperCommand sent = gripper.getCommand();
   EXPECT_TRUE(sent.action.get(ActionRequestBit::GoTo));
   EXPECT_EQ(sent.force, kMaxRegister);
}

TEST_F(TestControl, move_to_times_out_when_the_fingers_never_stop)
{
   fakeServer.model.setNeverArrives();
   MotionResult result{};

   const auto elapsed =
      durationOf([&] { result = moveTo(gripper, kOpening, k2F85, kSpeed, kEffort, kTimeoutLongEnoughForAnExchange); });

   EXPECT_EQ(result.outcome, MotionOutcome::TimedOut);
   EXPECT_EQ(result.finalStatus.gripperStatus.objectDetection(), ObjectDetection::Moving);
   EXPECT_TRUE(result.finalStatus.gripperStatus.activated());
   EXPECT_EQ(result.finalStatus.positionRequestEcho, kOpeningRegister);
   EXPECT_GE(elapsed, kTimeoutLongEnoughForAnExchange);
   EXPECT_LT(elapsed, kTimeoutLongEnoughForAnExchange + kLongestOverrun);

   const GripperCommand stillRequested = gripper.getCommand();
   EXPECT_TRUE(stillRequested.action.get(ActionRequestBit::GoTo));
   EXPECT_EQ(stillRequested.positionRequest, kOpeningRegister);
}

TEST_F(TestControl, move_to_times_out_when_the_gripper_never_echoes_the_request)
{
   GripperStatus stuck = gripper.getStatus();
   fake::setObjectDetection(stuck, ObjectDetection::AtRequestedPosition);
   ASSERT_NE(stuck.positionRequestEcho, kOpeningRegister);
   fakeServer.model.pinnedStatus = stuck;
   ASSERT_TRUE(waitFor(gripper, [&stuck](const StampedExchange& exchange) { return exchange.status == stuck; }, kWait));

   const MotionResult result = moveTo(gripper, kOpening, k2F85, kSpeed, kEffort, kShortTimeout);

   EXPECT_EQ(result.outcome, MotionOutcome::TimedOut);
   EXPECT_EQ(result.finalStatus, stuck);

   const GripperCommand stillRequested = gripper.getCommand();
   EXPECT_TRUE(stillRequested.action.get(ActionRequestBit::GoTo));
   EXPECT_EQ(stillRequested.positionRequest, kOpeningRegister);
}

TEST_F(TestControl, move_to_counts_the_wait_for_the_echo_in_its_timeout)
{
   fakeServer.model.setEchoLag(kEchoLagExchanges);
   fakeServer.model.setNeverArrives();
   MotionResult result{};
   ASSERT_NE(gripper.getStatus().positionRequestEcho, k2F85.closedPosition);
   bool isOpenRequested = true;
   const auto nextOpeningDifferentFromTheLastRequest = [&isOpenRequested] {
      isOpenRequested = !isOpenRequested;
      return isOpenRequested ? k2F85.maxOpening : k2F85.minOpening;
   };

   const bool isEchoWaitCounted = finishesWithin(kLatestReturnWithTheEchoCounted, [&] {
      result =
         moveTo(gripper, nextOpeningDifferentFromTheLastRequest(), k2F85, kSpeed, kEffort, kTimeoutPastTheEchoLag);
   });

   EXPECT_TRUE(isEchoWaitCounted);
   EXPECT_EQ(result.outcome, MotionOutcome::TimedOut);
}

TEST_F(TestControl, move_to_does_not_wait_for_the_motion_to_start_past_its_timeout)
{
   MotionResult result{};

   const bool isStartWindowCutShort = finishesWithin(kStartWindowFloor, [&] {
      result = moveTo(gripper, kOpening, k2F85, kSpeed, kEffort, kTimeoutBelowTheStartWindow);
   });

   EXPECT_TRUE(isStartWindowCutShort);
   EXPECT_NE(result.outcome, MotionOutcome::NotSent);
   EXPECT_EQ(gripper.getCommand().positionRequest, kOpeningRegister);
}

TEST_F(TestControl, open_and_close_stop_waiting_at_their_timeout)
{
   fakeServer.model.setNeverArrives();
   MotionResult opened{};
   MotionResult closed{};

   const auto elapsed = durationOf([&] {
      opened = open(gripper, k2F85, kSpeed, kEffort, kShortTimeout);
      closed = close(gripper, k2F85, kSpeed, kEffort, kShortTimeout);
   });

   EXPECT_EQ(opened.outcome, MotionOutcome::TimedOut);
   EXPECT_EQ(closed.outcome, MotionOutcome::TimedOut);
   EXPECT_LT(elapsed, 2 * (kShortTimeout + kLongestOverrun));
}

TEST_F(TestControl, a_timeout_that_leaves_no_time_sends_nothing)
{
   const GripperCommand before = gripper.getCommand();

   EXPECT_EQ(moveTo(gripper, kOpening, k2F85, kSpeed, kEffort, 0ms).outcome, MotionOutcome::NotSent);
   EXPECT_EQ(moveTo(gripper, kOpening, k2F85, kSpeed, kEffort, -1ms).outcome, MotionOutcome::NotSent);
   EXPECT_EQ(open(gripper, k2F85, kSpeed, kEffort, 0ms).outcome, MotionOutcome::NotSent);
   EXPECT_EQ(close(gripper, k2F85, kSpeed, kEffort, 0ms).outcome, MotionOutcome::NotSent);

   EXPECT_EQ(gripper.getCommand(), before);
}

TEST_F(TestControl, move_to_sends_nothing_for_a_quantity_with_no_register_value)
{
   constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
   const GripperCommand before = gripper.getCommand();

   const MotionResult result = moveTo(gripper, kOpening, k2F85, -kSpeed, kEffort);
   EXPECT_EQ(result.outcome, MotionOutcome::NotSent);
   EXPECT_EQ(result.finalStatus.gripperStatus.activationState(), ActivationState::Complete);

   EXPECT_EQ(moveTo(gripper, kOpening, k2F85, kNaN, kEffort).outcome, MotionOutcome::NotSent);
   EXPECT_EQ(moveTo(gripper, kOpening, k2F85, kSpeed, -kEffort).outcome, MotionOutcome::NotSent);
   EXPECT_EQ(moveTo(gripper, kOpening, k2F85, kSpeed, kNaN).outcome, MotionOutcome::NotSent);
   EXPECT_EQ(moveTo(gripper, kNaN, k2F85, kSpeed, kEffort).outcome, MotionOutcome::NotSent);

   EXPECT_EQ(gripper.getCommand(), before);
}

TEST_F(TestControl, move_to_brings_out_of_range_quantities_to_the_nearest_end)
{
   constexpr double kPastFullyOpen = 1.0;
   constexpr double kAboveMaxSpeed = 10.0;
   constexpr double kAboveFullEffort = 2.0;

   const MotionResult result = moveTo(gripper, kPastFullyOpen, k2F85, kAboveMaxSpeed, kAboveFullEffort);

   EXPECT_EQ(result.outcome, MotionOutcome::Reached);
   const GripperCommand sent = gripper.getCommand();
   EXPECT_EQ(sent.positionRequest, k2F85.openPosition);
   EXPECT_EQ(sent.speed, kMaxRegister);
   EXPECT_EQ(sent.force, kMaxRegister);
}

TEST_F(TestControl, open_requests_the_fully_open_position)
{
   const MotionResult result = open(gripper, k2F85, kSpeed, kEffort);

   EXPECT_EQ(result.outcome, MotionOutcome::Reached);
   const GripperCommand sent = gripper.getCommand();
   EXPECT_EQ(sent.positionRequest, k2F85.openPosition);
   EXPECT_EQ(sent.speed, kSpeedRegister);
   EXPECT_EQ(sent.force, kEffortRegister);
}

TEST_F(TestControl, close_requests_the_fully_closed_position)
{
   const MotionResult result = close(gripper, k2F85, kSpeed, kEffort);

   EXPECT_EQ(result.outcome, MotionOutcome::Reached);
   const GripperCommand sent = gripper.getCommand();
   EXPECT_EQ(sent.positionRequest, k2F85.closedPosition);
   EXPECT_EQ(sent.speed, kSpeedRegister);
   EXPECT_EQ(sent.force, kEffortRegister);
}

TEST_F(TestControl, get_state_reports_the_opening_in_metres)
{
   ASSERT_EQ(moveTo(gripper, kOpening, k2F85, kSpeed, kEffort).outcome, MotionOutcome::Reached);

   const State state = getState(gripper, k2F85);

   ASSERT_TRUE(state.openingMetres.has_value());
   EXPECT_NEAR(*state.openingMetres, kOpening, kOpeningTolerance);
   EXPECT_EQ(state.objectDetection, ObjectDetection::AtRequestedPosition);
}

TEST_F(TestControl, get_state_reports_a_held_object)
{
   fakeServer.model.setObjectAt(kObjectRegister);
   ASSERT_EQ(close(gripper, k2F85, kSpeed, kEffort).outcome, MotionOutcome::StoppedOnObject);

   const State state = getState(gripper, k2F85);

   ASSERT_TRUE(state.openingMetres.has_value());
   EXPECT_NEAR(*state.openingMetres, kObjectWidth, kOpeningTolerance);
   EXPECT_EQ(state.objectDetection, ObjectDetection::DetectedWhileClosing);
}

TEST_F(TestControl, get_state_has_no_opening_for_a_profile_with_no_travel)
{
   DeviceProfile noTravel = k2F85;
   noTravel.closedPosition = noTravel.openPosition;
   ASSERT_EQ(moveTo(gripper, kOpening, k2F85, kSpeed, kEffort).outcome, MotionOutcome::Reached);

   const State state = getState(gripper, noTravel);

   EXPECT_FALSE(state.openingMetres.has_value());
   EXPECT_EQ(state.objectDetection, ObjectDetection::AtRequestedPosition);
}

TEST_F(TestControl, get_health_of_an_activated_fault_free_gripper)
{
   const Health health = getHealth(gripper);

   EXPECT_TRUE(health.reachable);
   EXPECT_TRUE(health.active);
   EXPECT_EQ(health.fault.gripperFault(), GripperFault::None);
   EXPECT_FALSE(health.needsRecover);
}

TEST_F(TestControl, get_health_asks_for_recovery_on_a_major_fault)
{
   latchFault(GripperFault::Overcurrent);

   const Health health = getHealth(gripper);

   EXPECT_TRUE(health.reachable);
   EXPECT_EQ(health.fault.gripperFault(), GripperFault::Overcurrent);
   EXPECT_TRUE(health.needsRecover);
}

TEST_F(TestControl, get_health_does_not_ask_for_recovery_after_a_communication_gap)
{
   latchFault(GripperFault::NoCommunication);

   const Health health = getHealth(gripper);

   EXPECT_EQ(health.fault.gripperFault(), GripperFault::NoCommunication);
   EXPECT_FALSE(health.needsRecover);
}

TEST_F(TestControl, get_health_is_not_active_while_the_activation_is_in_progress)
{
   GripperStatus activating = gripper.getStatus();
   fake::setActivationState(activating, ActivationState::InProgress);
   ASSERT_TRUE(activating.gripperStatus.activated());
   fakeServer.model.pinnedStatus = activating;
   ASSERT_TRUE(waitFor(
      gripper,
      [](const StampedExchange& exchange) {
         return exchange.status.gripperStatus.activationState() == ActivationState::InProgress;
      },
      kWait));

   EXPECT_FALSE(getHealth(gripper).active);
}

TEST(TestControlHealth, a_gripper_never_activated_is_not_active)
{
   InstrumentedFakeGripperServer fakeServer;
   const Gripper gripper = makeGripper(fakeServer, kFastPeriod);

   const Health health = getHealth(gripper);

   EXPECT_TRUE(health.reachable);
   EXPECT_FALSE(health.active);
   EXPECT_FALSE(health.needsRecover);
}

TEST(TestControlMotion, move_to_does_not_activate_a_gripper_that_is_not)
{
   InstrumentedFakeGripperServer fakeServer;
   Gripper gripper = makeGripper(fakeServer, kFastPeriod);

   const MotionResult result = moveTo(gripper, kOpening, k2F85, kSpeed, kEffort, kShortTimeout);

   EXPECT_EQ(result.outcome, MotionOutcome::TimedOut);
   EXPECT_FALSE(gripper.getCommand().action.get(ActionRequestBit::Activate));
   EXPECT_FALSE(getHealth(gripper).active);
}

TEST(TestControlHealth, a_dead_link_is_not_reachable)
{
   InstrumentedFakeGripperServer fakeServer;
   const auto gripper = makeStalledGripper(fakeServer);

   EXPECT_FALSE(getHealth(*gripper).reachable);
}

TEST(TestControlState, get_state_and_get_health_answer_at_once_on_a_dead_link)
{
   InstrumentedFakeGripperServer fakeServer;
   const auto gripper = makeStalledGripper(fakeServer);

   const bool isAnsweredAtOnce = finishesWithin(kAtOnce, [&] {
      static_cast<void>(getState(*gripper, k2F85));
      static_cast<void>(getHealth(*gripper));
   });

   EXPECT_TRUE(isAnsweredAtOnce);
}

TEST_F(TestControl, recover_from_fault_clears_the_fault_get_health_reported)
{
   latchFault(GripperFault::Overcurrent);
   ASSERT_TRUE(getHealth(gripper).needsRecover);

   EXPECT_EQ(recoverFromFault(gripper, kWait), ActivationResult::Activated);

   EXPECT_EQ(fakeServer.model.resets.load(), 1);
   const Health health = getHealth(gripper);
   EXPECT_TRUE(health.active);
   EXPECT_EQ(health.fault.gripperFault(), GripperFault::None);
   EXPECT_FALSE(health.needsRecover);
}

TEST(TestControlNames, motion_outcome_names_are_usable_at_compile_time)
{
   static_assert(toString(MotionOutcome::Reached) == "Reached");
   static_assert(toString(MotionOutcome::StoppedOnObject) == "StoppedOnObject");
   static_assert(toString(MotionOutcome::TimedOut) == "TimedOut");
   static_assert(toString(MotionOutcome::NotSent) == "NotSent");
   static_assert(toString(static_cast<MotionOutcome>(0x7F)) == "Unrecognized");
}

} // namespace Robotiq::test
