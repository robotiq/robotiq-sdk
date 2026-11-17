// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

//! \brief What SerialPort throws. Internal: each SDK translates it at its
//! boundary.

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
