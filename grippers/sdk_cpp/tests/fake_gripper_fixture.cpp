// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include "fake_gripper_fixture.hpp"

#include "fake/status_writer.hpp"

namespace Robotiq::test {

void InstrumentedRegisterModel::setActivated()
{
   GripperStatus newStatus = status();
   fake::setActivated(newStatus, true);
   fake::setActivationState(newStatus, ActivationState::Complete);
   setStatus(newStatus);
   setActivationDone(true);
}

void InstrumentedRegisterModel::setFault(GripperFault fault)
{
   GripperStatus newStatus = status();
   fake::setGripperFault(newStatus, fault);
   setStatus(newStatus);
}

void InstrumentedRegisterModel::setGoToEcho()
{
   GripperStatus newStatus = status();
   fake::setGoToEnabled(newStatus, true);
   setStatus(newStatus);
}

void InstrumentedRegisterModel::setPositionRequestEcho(uint8_t position)
{
   GripperStatus newStatus = status();
   newStatus.positionRequestEcho = position;
   setStatus(newStatus);
}

void InstrumentedRegisterModel::setActivationDone(bool value)
{
   _previousActivateBit = value;
   _activationDone = value;
}

void InstrumentedRegisterModel::setActivationHighButIncomplete()
{
   _previousActivateBit = true;
   _activationDone = false;
}

void InstrumentedRegisterModel::setObjectAt(uint8_t position)
{
   _objectAt = position;
}

void InstrumentedRegisterModel::setOpeningBlockedAt(uint8_t position)
{
   _openingBlockedAt = position;
}

void InstrumentedRegisterModel::setNeverArrives()
{
   _neverArrives = true;
}

void InstrumentedRegisterModel::setMotionLag(int exchanges)
{
   _motionLag = exchanges;
}

void InstrumentedRegisterModel::setEchoLag(int exchanges)
{
   _echoLag = exchanges;
}

void InstrumentedRegisterModel::read(uint16_t address, uint16_t quantity, uint16_t* out) const
{
   statusReads.fetch_add(1);
   RegisterModel::read(address, quantity, out);
}

void InstrumentedRegisterModel::write(uint16_t address, uint16_t quantity, const uint16_t* values)
{
   commandWrites.fetch_add(1);
   RegisterModel::write(address, quantity, values);
}

GripperStatus InstrumentedRegisterModel::processCommand(const GripperCommand& command,
                                                        const GripperStatus& currentStatus)
{
   // The rACT falling edge is the reset request. Derived here rather than
   // signalled by the model, so the model owes tests no hook at all.
   if(_previousActivateBit && !command.action.get(ActionRequestBit::Activate))
   {
      resets.fetch_add(1);
   }
   // The model runs even while pinned: it is what tracks the rACT edge, so
   // skipping it would leave a stale edge behind once the pin is released.
   const GripperStatus modelled = RegisterModel::processCommand(command, currentStatus);
   if(pinnedStatus)
   {
      return *pinnedStatus;
   }
   return modelled.gripperStatus.goToEnabled() ? withStagedMotion(modelled, currentStatus) : modelled;
}

void InstrumentedRegisterModel::countExchangesSinceRequest(uint8_t positionRequest)
{
   if(_latestRequest != positionRequest)
   {
      _latestRequest = positionRequest;
      _exchangesSinceRequest = 0;
   }
   else if(_exchangesSinceRequest < kExchangeCountCeiling)
   {
      ++_exchangesSinceRequest;
   }
}

GripperStatus InstrumentedRegisterModel::withStagedMotion(const GripperStatus& modelled,
                                                          const GripperStatus& currentStatus)
{
   countExchangesSinceRequest(modelled.positionRequestEcho);
   if(_exchangesSinceRequest < _echoLag)
   {
      return currentStatus;
   }

   const GripperStatus atRest = withObstacles(modelled, currentStatus);
   const int startsMovingAfter = _motionLag;
   const int stopsMovingAfter = 2 * startsMovingAfter;
   if(_exchangesSinceRequest >= stopsMovingAfter)
   {
      return atRest;
   }

   const bool hasStartedMoving = _exchangesSinceRequest >= startsMovingAfter;
   GripperStatus inTransit = atRest;
   inTransit.position = currentStatus.position;
   fake::setObjectDetection(inTransit,
                            hasStartedMoving ? ObjectDetection::Moving : currentStatus.gripperStatus.objectDetection());
   return inTransit;
}

GripperStatus InstrumentedRegisterModel::withObstacles(const GripperStatus& modelled,
                                                       const GripperStatus& currentStatus) const
{
   GripperStatus obstructed = modelled;
   const int objectAt = _objectAt;
   const int openingBlockedAt = _openingBlockedAt;
   if(_neverArrives)
   {
      fake::setObjectDetection(obstructed, ObjectDetection::Moving);
      obstructed.position = currentStatus.position;
   }
   else if(objectAt != kNoObstacle && modelled.position > objectAt)
   {
      fake::setObjectDetection(obstructed, ObjectDetection::DetectedWhileClosing);
      obstructed.position = static_cast<uint8_t>(objectAt);
   }
   else if(openingBlockedAt != kNoObstacle && modelled.position < openingBlockedAt)
   {
      fake::setObjectDetection(obstructed, ObjectDetection::DetectedWhileOpening);
      obstructed.position = static_cast<uint8_t>(openingBlockedAt);
   }
   return obstructed;
}
} // namespace Robotiq::test
