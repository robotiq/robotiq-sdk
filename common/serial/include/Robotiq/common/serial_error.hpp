// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief The one error SerialPort throws.
//! Internal like SerialPort itself: each SDK catches it at its own
//! boundary and reports it its own way (the grippers SDK as
//! SerialIOException, the TSF SDK through getLastError()), so it never
//! reaches a consumer.

#pragma once

#include <stdexcept>
#include <string>

namespace Robotiq::common {
class SerialError : public std::runtime_error
{
public:
   explicit SerialError(const std::string& description)
      : std::runtime_error(description)
   {
   }
};
} // namespace Robotiq::common
