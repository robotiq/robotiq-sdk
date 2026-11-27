// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <string_view>

namespace Robotiq::detail {
//! How a blocking motion — moveTo(), open() or close() — ended.
enum class MotionOutcome
{
   Reached, //!< the fingers stopped at the requested opening
   StoppedOnObject, //!< the fingers stopped on an object before the requested opening
   TimedOut, //!< the command never arrived, or the fingers never stopped
             //!< before the timeout elapsed. Compare the final status's
             //!< positionRequestEcho with the requested position: equal
             //!< means the fingers never stopped, different means the
             //!< command never arrived. This holds only if the request
             //!< differed from the echo before the call
   NotSent, //!< the opening, speed or effort has no register value (NaN,
            //!< infinite, or a negative speed or effort), or the timeout
            //!< is zero or negative; nothing was sent
};

//! \param outcome Outcome of moveTo(), open() or close().
//! \return A short name for \p outcome.
[[nodiscard]] constexpr std::string_view toString(MotionOutcome outcome)
{
   switch(outcome)
   {
   case MotionOutcome::Reached:
      return "Reached";
   case MotionOutcome::StoppedOnObject:
      return "StoppedOnObject";
   case MotionOutcome::TimedOut:
      return "TimedOut";
   case MotionOutcome::NotSent:
      return "NotSent";
   }
   return "Unrecognized";
}
} // namespace Robotiq::detail
