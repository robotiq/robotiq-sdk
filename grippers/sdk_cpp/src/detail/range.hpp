// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <algorithm>
#include <limits>

namespace Robotiq::detail {

template <class T>
class Range
{
public:
   constexpr Range(T lower, T upper)
      : _lower{lower}
      , _upper{upper}
   {
   }

   [[nodiscard]] constexpr T lower() const { return _lower; }
   [[nodiscard]] constexpr T upper() const { return _upper; }

   [[nodiscard]] constexpr double span() const { return static_cast<double>(_upper) - static_cast<double>(_lower); }

   [[nodiscard]] constexpr double ratioAt(T value) const
   {
      return (static_cast<double>(value) - static_cast<double>(_lower)) / span();
   }

   [[nodiscard]] constexpr double clampedRatioAt(T value) const { return std::clamp(ratioAt(value), 0.0, 1.0); }

   [[nodiscard]] constexpr double valueAt(double ratio) const { return static_cast<double>(_lower) + ratio * span(); }

   [[nodiscard]] constexpr bool isValid() const { return span() > 0.0 && span() <= std::numeric_limits<double>::max(); }

private:
   T _lower;
   T _upper;
};

} // namespace Robotiq::detail
