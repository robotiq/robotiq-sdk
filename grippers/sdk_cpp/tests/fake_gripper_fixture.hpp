// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief Test-only instrumentation and staging for the fake gripper.
//! The library ships three pieces that each do one job — RegisterModel
//! (behaviour), GripperServer (Modbus server), GripperSerial
//! (transport) — and none of them carries anything that exists purely for a
//! test. Everything of that kind lives here instead: transaction counters,
//! shortcuts for starting the device in a state a real gripper would have
//! reached before we connected, and a way to wedge it.
//!
//! InstrumentedRegisterModel derives from the shipped model and layers those on
//! through the hooks it provides, so tests get their seams without the
//! production classes knowing that tests exist.

#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include <Robotiq/gripper/command.hpp>
#include <Robotiq/gripper/fault_status.hpp>
#include <Robotiq/gripper/status.hpp>

#include "fake/gripper_serial.hpp"
#include "fake/gripper_server.hpp"
#include "fake/register_model.hpp"

namespace Robotiq::test {
using fake::GripperServer;

class InstrumentedRegisterModel : public fake::RegisterModel
{
public:
   //! Transaction counters, read from the test thread while the exchange
   //! thread drives the server. Each rACT falling edge is a reset request, so
   //! `resets` distinguishes a skipped handshake from a real one.
   mutable std::atomic<int> statusReads{0};
   std::atomic<int> commandWrites{0};
   std::atomic<int> resets{0};

   //! Pin the reported status regardless of the command — a gripper stuck in
   //! some state, e.g. never finishing its activation.
   std::optional<GripperStatus> pinnedStatus;

   void setActivated();
   void setFault(GripperFault fault);
   void setGoToEcho();
   void setPositionRequestEcho(uint8_t position);
   void setActivationDone(bool value);

   //! A gripper whose rACT was already high before the SDK attached, but whose
   //! activation is not complete — the power-cycled case. rACT is remembered as
   //! high so the command seeded from the echoed gACT does not read as a rising
   //! edge, while the activation stays incomplete: only the host's
   //! clear-then-set handshake can finish it.
   //!
   //! setActivationDone(true) cannot express this. It marks the activation done
   //! as well, so the first exchange recomputes the status as Complete and the
   //! injected reset state survives only until then — whichever of the exchange
   //! thread and the caller gets there first decides what activate() sees.
   void setActivationHighButIncomplete();

   //! An object sits between the fingers at \p position: a motion asked to
   //! close past it stops there and reports DetectedWhileClosing.
   void setObjectAt(uint8_t position);

   //! Something blocks the fingers at \p position on their way open: a motion
   //! asked to open past it stops there and reports DetectedWhileOpening.
   void setOpeningBlockedAt(uint8_t position);

   //! The fingers never stop: every motion keeps reporting Moving, from
   //! wherever they were. Overrides setObjectAt() and setOpeningBlockedAt().
   void setNeverArrives();

   //! A new position request takes time to show, as on a real gripper: for
   //! \p exchanges exchanges the status still holds the previous position
   //! and object detection, then reports Moving for as many, then arrives.
   void setMotionLag(int exchanges);

   //! The gripper is slow to take a new position request into account: for
   //! \p exchanges exchanges the status does not change at all, so the
   //! request is not echoed yet.
   void setEchoLag(int exchanges);

   void read(uint16_t address, uint16_t quantity, uint16_t* out) const override;
   void write(uint16_t address, uint16_t quantity, const uint16_t* values) override;

protected:
   GripperStatus processCommand(const GripperCommand& command, const GripperStatus& currentStatus) override;

private:
   void countExchangesSinceRequest(uint8_t positionRequest);
   GripperStatus withStagedMotion(const GripperStatus& modelled, const GripperStatus& currentStatus);
   GripperStatus withObstacles(const GripperStatus& modelled, const GripperStatus& currentStatus) const;

   static constexpr int kNoObstacle = -1;
   static constexpr int kExchangeCountCeiling = 1 << 20;

   std::atomic<int> _objectAt{kNoObstacle};
   std::atomic<int> _openingBlockedAt{kNoObstacle};
   std::atomic<bool> _neverArrives{false};
   std::atomic<int> _motionLag{0};
   std::atomic<int> _echoLag{0};

   std::optional<uint8_t> _latestRequest;
   int _exchangesSinceRequest = 0;
};

//! An instrumented model and the Modbus server in front of it, as one object,
//! so a test can declare a device in one line and still reach both halves.
struct InstrumentedFakeGripperServer
{
   explicit InstrumentedFakeGripperServer(uint8_t slaveAddress = 0x09)
      : server(model, slaveAddress)
   {
   }

   InstrumentedRegisterModel model;
   GripperServer server;
};
} // namespace Robotiq::test
