// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief One blocking call per action: move, open, close, read the state,
//!        read the health. Internal: the shape of these functions is not
//!        settled, and it may change in any release.
//!
//! open() and close() share their names with the operating system's file
//! functions: inside namespace Robotiq::detail, write the operating
//! system's ones ::open() and ::close().

#pragma once

#include <chrono>
#include <optional>

#include <Robotiq/gripper.hpp>
#include <Robotiq/gripper/device_profile.hpp>
#include <Robotiq/gripper/fault_status.hpp>
#include <Robotiq/detail/motion_outcome.hpp>
#include <Robotiq/gripper/status.hpp>

namespace Robotiq::detail {

//! How long moveTo(), open() and close() let a motion take unless told
//! otherwise.
inline constexpr std::chrono::seconds kDefaultMotionTimeout{10};

//! What moveTo(), open() and close() return.
struct MotionResult
{
   MotionOutcome outcome; //!< how the motion ended
   GripperStatus finalStatus; //!< the status that decided the outcome: the
                              //!< exchange on which the fingers stopped, or
                              //!< the latest status when nothing was sent or
                              //!< the timeout elapsed
};

//! \brief Move the fingers to an opening and block until the motion ends.
//!
//! Sends the request, waits for the gripper to echo it, then for the
//! fingers to stop, at the requested opening or on an object. The command
//! block is the gripper's current one with the position, speed, force and
//! rGTO replaced, so activation and the other action bits are kept.
//!
//! The quantities convert through units::openingToRegister(),
//! units::speedToRegister() and units::effortToRegister(), with their
//! rules: an opening outside the profile's range reads as the nearest end,
//! a speed outside the profile's range is brought to the nearest end, and
//! an effort above 1 saturates. A quantity with no register value — NaN,
//! infinity, a negative speed or effort — sends nothing and reports
//! MotionOutcome::NotSent. Never throws on a bad quantity.
//!
//! The gripper must be activated first, see activate(): one that is not
//! does not move, and the call reports MotionOutcome::TimedOut.
//!
//! A gripper whose fingers have stopped ignores a new speed or effort for
//! the opening it was already asked for. Asking again for that opening
//! starts no motion and reports the stop already reached, so to grip a held
//! part harder, move to another opening first.
//!
//! The request stays in force after MotionOutcome::TimedOut: the command
//! is not withdrawn, so the fingers may still move afterwards, for example
//! when a lost link comes back. Send another command to replace it.
//!
//! The three quantities are plain numbers in a fixed order: opening, then
//! speed, then effort. Nothing catches two of them swapped.
//! \param gripper The gripper to move. The caller is its one commander for
//!        the duration of the call; see Gripper.
//! \param openingMetres Distance between the fingers to reach, in metres.
//! \param profile The device profile of the gripper model and its fingers.
//! \param speedMetresPerSecond Closing or opening speed, in m/s.
//! \param effort Gripping effort, 0 (the gripper's minimum force) to 1 (its
//!        maximum).
//! \param timeout How long the whole motion may take, starting now. Zero
//!        or less sends nothing and reports MotionOutcome::NotSent.
//! \return How the motion ended, with the status that decided it.
MotionResult moveTo(Gripper& gripper,
                    double openingMetres,
                    const DeviceProfile& profile,
                    double speedMetresPerSecond,
                    double effort,
                    std::chrono::milliseconds timeout = kDefaultMotionTimeout);

//! \brief Open the fingers fully: moveTo() the profile's maxOpening.
//! \param gripper The gripper to open.
//! \param profile The device profile of the gripper model and its fingers.
//! \param speedMetresPerSecond Opening speed, in m/s.
//! \param effort Effort, 0 to 1; see moveTo().
//! \param timeout How long the whole motion may take, starting now.
//! \return How the motion ended; see moveTo().
MotionResult open(Gripper& gripper,
                  const DeviceProfile& profile,
                  double speedMetresPerSecond,
                  double effort,
                  std::chrono::milliseconds timeout = kDefaultMotionTimeout);

//! \brief Close the fingers fully: moveTo() the profile's minOpening.
//!
//! Closing on a part reports MotionOutcome::StoppedOnObject: that is a
//! successful grasp, not a failure.
//! \param gripper The gripper to close.
//! \param profile The device profile of the gripper model and its fingers.
//! \param speedMetresPerSecond Closing speed, in m/s.
//! \param effort Gripping effort, 0 to 1; see moveTo().
//! \param timeout How long the whole motion may take, starting now.
//! \return How the motion ended; see moveTo().
MotionResult close(Gripper& gripper,
                   const DeviceProfile& profile,
                   double speedMetresPerSecond,
                   double effort,
                   std::chrono::milliseconds timeout = kDefaultMotionTimeout);

//! Where the fingers are and whether they hold something; see getState().
struct State
{
   //! Distance between the fingers, in metres; empty when the profile given
   //! to getState() is one the conversion cannot use. See
   //! units::openingFromRegister().
   std::optional<double> openingMetres;

   //! The gripper's own object detection (gOBJ). DetectedWhileClosing or
   //! DetectedWhileOpening means the fingers stopped on an object.
   ObjectDetection objectDetection;
};

//! \brief Read the opening and the object detection from the latest status.
//!
//! Returns at once: it reads the process image and never touches the bus.
//! \param gripper The gripper to read.
//! \param profile The device profile of the gripper model and its fingers.
//! \return The state the latest exchange reported.
[[nodiscard]] State getState(const Gripper& gripper, const DeviceProfile& profile);

//! Whether the gripper can take a motion command; see getHealth().
struct Health
{
   bool reachable; //!< the exchange cycle is succeeding (ConnectionState::Operational)
   bool active; //!< the activation is complete (ActivationState::Complete)
   FaultStatus fault; //!< the fault codes of the latest status
   bool needsRecover; //!< a major gripper fault is latched: only recoverFromFault() clears it
};

//! \brief Read the link, activation and fault state from the latest status.
//!
//! Returns at once: it reads the process image and never touches the bus.
//! When \ref Health::reachable is false the other fields describe the last
//! status received, which may be stale.
//!
//! Only a fault of FaultSeverity::Major sets \ref Health::needsRecover. A
//! minor one such as GripperFault::NoCommunication, which a gripper reports
//! after a second without an exchange, clears on its own.
//! \param gripper The gripper to read.
//! \return The health the latest exchange reported.
[[nodiscard]] Health getHealth(const Gripper& gripper);

} // namespace Robotiq::detail
