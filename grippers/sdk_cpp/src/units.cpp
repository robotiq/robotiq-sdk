// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <Robotiq/gripper/units.hpp>

#include <Robotiq/detail/default_logger.hpp>
#include <Robotiq/gripper/logger.hpp>

#include "detail/range.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace Robotiq::units {

namespace {
using detail::Range;

constexpr Range<uint8_t> positionBand(const DeviceProfile& profile)
{
   return {profile.openPosition, profile.closedPosition};
}

constexpr Range<double> openingSpan(const DeviceProfile& profile)
{
   return {profile.minOpening, profile.maxOpening};
}

uint8_t roundedRegister(double steps)
{
   return static_cast<uint8_t>(std::lround(steps));
}

std::optional<uint8_t> registerFromSpan(double value, const Range<double>& span)
{
   if(!std::isfinite(value) || value < 0.0 || !span.isValid())
   {
      return std::nullopt;
   }
   return roundedRegister(span.clampedRatioAt(value) * 0xFF);
}

std::optional<double> closingRatio(uint8_t value, const DeviceProfile& profile)
{
   if(!positionBand(profile).isValid())
   {
      return std::nullopt;
   }
   return positionBand(profile).clampedRatioAt(value);
}

std::optional<uint8_t> registerAtClosingRatio(double ratio, const DeviceProfile& profile)
{
   if(!positionBand(profile).isValid())
   {
      return std::nullopt;
   }
   return roundedRegister(positionBand(profile).valueAt(std::clamp(ratio, 0.0, 1.0)));
}
} // namespace

std::optional<uint8_t> registerFromSpan(double value, double minimum, double maximum)
{
   return registerFromSpan(value, {minimum, maximum});
}

std::optional<uint8_t> speedToRegister(double speed, const DeviceProfile& profile)
{
   return registerFromSpan(speed, {profile.minSpeed, profile.maxSpeed});
}

std::optional<uint8_t> normalizedToRegister(double value, const std::shared_ptr<Logger>& logger)
{
   if(!std::isfinite(value))
   {
      return std::nullopt;
   }
   const double clamped = std::clamp(value, 0.0, 1.0);
   if(clamped != value)
   {
      const std::shared_ptr<Logger> sink = logger ? logger : detail::makeDefaultLogger();
      sink->log(Logger::Level::Warn,
                "normalized position " + std::to_string(value) + " is outside [0, 1]; clamped to "
                   + std::to_string(clamped));
   }
   return roundedRegister(clamped * 0xFF);
}

double normalizedFromRegister(uint8_t value)
{
   return value / 255.0;
}

std::optional<uint8_t> openingToRegister(double opening, const DeviceProfile& profile)
{
   const Range<double> span = openingSpan(profile);
   if(!std::isfinite(opening) || !span.isValid())
   {
      return std::nullopt;
   }
   return registerAtClosingRatio(1.0 - span.clampedRatioAt(opening), profile);
}

std::optional<double> openingFromRegister(uint8_t value, const DeviceProfile& profile)
{
   const Range<double> span = openingSpan(profile);
   const std::optional<double> closing = closingRatio(value, profile);
   if(!closing || !span.isValid())
   {
      return std::nullopt;
   }
   return span.valueAt(1.0 - *closing);
}

std::optional<uint8_t> knuckleAngleToRegister(double angle, const DeviceProfile& profile)
{
   const Range<double> sweep{0.0, profile.knuckleAngleRange()};
   if(!std::isfinite(angle) || !sweep.isValid())
   {
      return std::nullopt;
   }
   return registerAtClosingRatio(sweep.clampedRatioAt(angle), profile);
}

std::optional<double> knuckleAngleFromRegister(uint8_t value, const DeviceProfile& profile)
{
   const Range<double> sweep{0.0, profile.knuckleAngleRange()};
   const std::optional<double> closing = closingRatio(value, profile);
   if(!closing || !sweep.isValid())
   {
      return std::nullopt;
   }
   return sweep.valueAt(*closing);
}

std::optional<uint8_t> effortToRegister(double effort)
{
   return registerFromSpan(effort, {0.0, 1.0});
}

} // namespace Robotiq::units
