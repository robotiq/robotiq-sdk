// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <Robotiq/detail/control.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>

#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/connection_state.hpp>
#include <Robotiq/gripper/fault_status.hpp>
#include <Robotiq/gripper/stamped_exchange.hpp>
#include <Robotiq/gripper/status.hpp>
#include <Robotiq/gripper/units.hpp>
#include <Robotiq/gripper/wait.hpp>

namespace Robotiq::detail {
namespace {
constexpr std::chrono::milliseconds kMotionStartWindow{200};

MotionOutcome outcomeOf(ObjectDetection detection)
{
   return detection == ObjectDetection::AtRequestedPosition ? MotionOutcome::Reached : MotionOutcome::StoppedOnObject;
}

std::optional<GripperCommand> motionCommand(const Gripper& gripper,
                                            double openingMetres,
                                            const DeviceProfile& profile,
                                            double speedMetresPerSecond,
                                            double effort)
{
   const std::optional<uint8_t> position = units::openingToRegister(openingMetres, profile);
   const std::optional<uint8_t> speed = units::speedToRegister(speedMetresPerSecond, profile);
   const std::optional<uint8_t> force = units::effortToRegister(effort);
   if(!position || !speed || !force)
   {
      return std::nullopt;
   }

   GripperCommand command = gripper.getCommand();
   command.action.set(ActionRequestBit::GoTo, true);
   command.positionRequest = *position;
   command.speed = *speed;
   command.force = *force;
   return command;
}

bool sendAndWaitForEcho(Gripper& gripper, const GripperCommand& command, std::chrono::steady_clock::time_point deadline)
{
   return setCommandAndWaitForExchange(gripper, command, remainingUntil(deadline))
       && waitForPositionEcho(gripper, command.positionRequest, remainingUntil(deadline));
}

void allowMotionToStart(const Gripper& gripper, std::chrono::steady_clock::time_point deadline)
{
   waitForObjectDetection(gripper, ObjectDetection::Moving, std::min(kMotionStartWindow, remainingUntil(deadline)));
}
} // namespace

MotionResult moveTo(Gripper& gripper,
                    double openingMetres,
                    const DeviceProfile& profile,
                    double speedMetresPerSecond,
                    double effort,
                    std::chrono::milliseconds timeout)
{
   const std::optional<GripperCommand> command =
      motionCommand(gripper, openingMetres, profile, speedMetresPerSecond, effort);
   const bool hasTime = timeout > std::chrono::milliseconds::zero();
   if(!command || !hasTime)
   {
      return {MotionOutcome::NotSent, gripper.getStatus()};
   }

   const auto deadline = deadlineAfter(timeout);
   if(!sendAndWaitForEcho(gripper, *command, deadline))
   {
      return {MotionOutcome::TimedOut, gripper.getStatus()};
   }

   allowMotionToStart(gripper, deadline);

   const std::optional<StampedExchange> settled = waitForMotionEnd(gripper, remainingUntil(deadline));
   if(!settled)
   {
      return {MotionOutcome::TimedOut, gripper.getStatus()};
   }
   return {outcomeOf(settled->status.gripperStatus.objectDetection()), settled->status};
}

MotionResult open(Gripper& gripper,
                  const DeviceProfile& profile,
                  double speedMetresPerSecond,
                  double effort,
                  std::chrono::milliseconds timeout)
{
   return moveTo(gripper, profile.maxOpening, profile, speedMetresPerSecond, effort, timeout);
}

MotionResult close(Gripper& gripper,
                   const DeviceProfile& profile,
                   double speedMetresPerSecond,
                   double effort,
                   std::chrono::milliseconds timeout)
{
   return moveTo(gripper, profile.minOpening, profile, speedMetresPerSecond, effort, timeout);
}

State getState(const Gripper& gripper, const DeviceProfile& profile)
{
   const GripperStatus status = gripper.getStatus();
   return {units::openingFromRegister(status.position, profile), status.gripperStatus.objectDetection()};
}

Health getHealth(const Gripper& gripper)
{
   const GripperStatus status = gripper.getStatus();
   const bool isReachable = gripper.connectionState() == ConnectionState::Operational;
   const bool isActive = status.gripperStatus.activationState() == ActivationState::Complete;
   const bool hasMajorFault = severity(status.faultStatus.gripperFault()) == FaultSeverity::Major;
   return {isReachable, isActive, status.faultStatus, hasMajorFault};
}

} // namespace Robotiq::detail
