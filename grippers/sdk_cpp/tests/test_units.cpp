// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <ostream>
#include <tuple>
#include <vector>

#include <Robotiq/gripper/device_profile.hpp>
#include <Robotiq/gripper/units.hpp>

#include "test_utils.hpp"

namespace Robotiq::test {

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();
const DeviceProfile& k2F85 = profiles::k2F85;
const DeviceProfile& kHandE = profiles::kHandE;
using namespace Robotiq::units;
} // namespace

// Each shipped profile: its manual ranges, its measured band, and an opening
// between the ends with the register it rounds to.
struct ShippedProfile
{
   const char* name;
   const DeviceProfile& profile;
   double minSpeed;
   double maxSpeed;
   double maxOpening;
   uint8_t openPosition;
   uint8_t closedPosition;
   double midOpening;
   uint8_t midPosition;
};

// Without this gtest dumps the raw parameter bytes into the test listing
// ctest reads.
void PrintTo(const ShippedProfile& shipped, std::ostream* out)
{
   *out << shipped.name;
}

const std::vector<ShippedProfile>& shippedProfiles()
{
   static const std::vector<ShippedProfile> kShipped = {
      {"k2F85", profiles::k2F85, 0.020, 0.150, 0.085, 3, 230, 0.0425, 117}, // 113.5 steps
      {"k2F140", profiles::k2F140, 0.030, 0.250, 0.140, 3, 230, 0.070, 117}, // 113.5 steps
      {"kHandE", profiles::kHandE, 0.020, 0.150, 0.050, 3, 250, 0.025, 127}, // 123.5 steps
      {"kHandE100", profiles::kHandE100, 0.020, 0.150, 0.100, 3, 250, 0.050, 127}, // 123.5 steps
      {"kHandEC10", profiles::kHandEC10, 0.014, 0.107, 0.040, 3, 250, 0.020, 127}, // 123.5 steps
   };
   return kShipped;
}

class ShippedProfileTest : public ::testing::TestWithParam<ShippedProfile>
{
};

TEST_P(ShippedProfileTest, carries_the_manual_ranges_and_the_measured_register_band)
{
   const ShippedProfile& shipped = GetParam();
   EXPECT_DOUBLE_EQ(shipped.profile.minSpeed, shipped.minSpeed);
   EXPECT_DOUBLE_EQ(shipped.profile.maxSpeed, shipped.maxSpeed);
   EXPECT_DOUBLE_EQ(shipped.profile.minOpening, 0.0);
   EXPECT_DOUBLE_EQ(shipped.profile.maxOpening, shipped.maxOpening);
   EXPECT_EQ(shipped.profile.openPosition, shipped.openPosition);
   EXPECT_EQ(shipped.profile.closedPosition, shipped.closedPosition);
   EXPECT_DOUBLE_EQ(shipped.profile.registerPositionRange(), shipped.closedPosition - shipped.openPosition);
}

TEST_P(ShippedProfileTest, the_speed_range_spans_the_whole_byte)
{
   const ShippedProfile& shipped = GetParam();
   EXPECT_EQ(speedToRegister(shipped.minSpeed, shipped.profile), 0);
   EXPECT_EQ(speedToRegister(shipped.maxSpeed, shipped.profile), 255);
   EXPECT_EQ(speedToRegister(shipped.maxSpeed + 0.05, shipped.profile), 255);
   EXPECT_FALSE(speedToRegister(-0.1, shipped.profile).has_value());
}

TEST_P(ShippedProfileTest, the_stroke_spans_the_register_band)
{
   const ShippedProfile& shipped = GetParam();
   EXPECT_EQ(openingToRegister(shipped.maxOpening, shipped.profile), shipped.openPosition);
   EXPECT_EQ(openingToRegister(0.0, shipped.profile), shipped.closedPosition);
   EXPECT_EQ(openingToRegister(shipped.midOpening, shipped.profile), shipped.midPosition);
   EXPECT_EQ(openingToRegister(shipped.maxOpening + 0.01, shipped.profile), shipped.openPosition);
   EXPECT_EQ(openingToRegister(-1e-4, shipped.profile), shipped.closedPosition);
}

TEST_P(ShippedProfileTest, the_register_band_spans_the_stroke)
{
   const ShippedProfile& shipped = GetParam();
   EXPECT_DOUBLE_EQ(openingFromRegister(shipped.openPosition, shipped.profile).value(), shipped.maxOpening);
   EXPECT_DOUBLE_EQ(openingFromRegister(shipped.closedPosition, shipped.profile).value(), 0.0);
   EXPECT_DOUBLE_EQ(openingFromRegister(0, shipped.profile).value(), shipped.maxOpening);
   EXPECT_DOUBLE_EQ(openingFromRegister(255, shipped.profile).value(), 0.0);
}

TEST_P(ShippedProfileTest, inside_the_band_a_position_read_back_commands_the_same_value)
{
   const ShippedProfile& shipped = GetParam();
   for(int count = shipped.openPosition; count <= shipped.closedPosition; ++count)
   {
      const auto raw = static_cast<uint8_t>(count);
      EXPECT_EQ(openingToRegister(openingFromRegister(raw, shipped.profile).value(), shipped.profile), raw)
         << "count " << count;
   }
}

INSTANTIATE_TEST_SUITE_P(EveryShippedProfile,
                         ShippedProfileTest,
                         ::testing::ValuesIn(shippedProfiles()),
                         [](const ::testing::TestParamInfo<ShippedProfile>& info) { return info.param.name; });

TEST(TestDeviceProfile, the_knuckle_sweeps_over_the_whole_register_band)
{
   EXPECT_DOUBLE_EQ(k2F85.knuckleAngleRange(), 0.7929);
   EXPECT_DOUBLE_EQ(profiles::k2F140.knuckleAngleRange(), 0.77);
   EXPECT_DOUBLE_EQ(kHandE.knuckleAngleRange(), 0.0); // a slider: no knuckle
   EXPECT_DOUBLE_EQ(profiles::kHandEC10.knuckleAngleRange(), 0.0);
}

TEST(TestDeviceProfile, a_profile_written_without_a_knuckle_slope_has_no_angle)
{
   constexpr DeviceProfile custom{0.020, 0.150, 0.0, 0.085, 5, 226};
   EXPECT_DOUBLE_EQ(custom.knuckleAnglePerCount, 0.0);
   EXPECT_FALSE(knuckleAngleToRegister(0.4, custom).has_value());
   EXPECT_EQ(openingToRegister(0.0, custom), 226);
}

TEST(TestNormalizedToRegister, the_unit_span_maps_onto_the_whole_byte_open_to_closed)
{
   EXPECT_EQ(normalizedToRegister(0.0), 0); // fully open
   EXPECT_EQ(normalizedToRegister(0.5), 128); // 127.5 steps
   EXPECT_EQ(normalizedToRegister(1.0), 255); // fully closed
}

TEST(TestNormalizedToRegister, clamps_outside_the_unit_span)
{
   EXPECT_EQ(normalizedToRegister(-1e-4), 0);
   EXPECT_EQ(normalizedToRegister(-2.0), 0);
   EXPECT_EQ(normalizedToRegister(1.0 + 1e-4), 255);
   EXPECT_EQ(normalizedToRegister(3.0), 255);
}

TEST(TestNormalizedToRegister, warns_when_it_clamps)
{
   const auto logger = std::make_shared<CollectingLogger>();
   EXPECT_EQ(normalizedToRegister(1.5, logger), 255);
   EXPECT_EQ(normalizedToRegister(-0.5, logger), 0);
   ASSERT_EQ(logger->lines.size(), 2u);
   EXPECT_EQ(logger->lines[0].first, Logger::Level::Warn);
   EXPECT_TRUE(logger->contains("outside [0, 1]"));
}

TEST(TestNormalizedToRegister, is_silent_inside_the_unit_span)
{
   const auto logger = std::make_shared<CollectingLogger>();
   for(const double value : {0.0, 0.5, 1.0})
   {
      std::ignore = normalizedToRegister(value, logger);
   }
   std::ignore = normalizedToRegister(kNaN, logger);
   EXPECT_TRUE(logger->lines.empty());
}

TEST(TestNormalizedToRegister, rejects_what_has_no_register_value)
{
   EXPECT_FALSE(normalizedToRegister(kNaN).has_value());
   EXPECT_FALSE(normalizedToRegister(kInf).has_value());
   EXPECT_FALSE(normalizedToRegister(-kInf).has_value());
}

TEST(TestNormalizedFromRegister, the_byte_ends_land_on_the_unit_span)
{
   EXPECT_DOUBLE_EQ(normalizedFromRegister(0), 0.0);
   EXPECT_DOUBLE_EQ(normalizedFromRegister(255), 1.0);
}

TEST(TestNormalizedFromRegister, the_hand_e_stops_read_inside_the_unit_span)
{
   EXPECT_NEAR(normalizedFromRegister(kHandE.openPosition), 0.0118, 1e-4);
   EXPECT_NEAR(normalizedFromRegister(kHandE.closedPosition), 0.9804, 1e-4);
}

TEST(TestNormalizedFromRegister, a_position_read_back_commands_the_same_value)
{
   for(int count = 0; count <= 0xFF; ++count)
   {
      const auto raw = static_cast<uint8_t>(count);
      EXPECT_EQ(normalizedToRegister(normalizedFromRegister(raw)), raw) << "count " << count;
   }
}

TEST(TestRegisterFromSpan, maps_the_span_onto_the_whole_byte)
{
   EXPECT_EQ(registerFromSpan(20.0, 20.0, 235.0), 0);
   EXPECT_EQ(registerFromSpan(127.5, 20.0, 235.0), 128); // 127.5 steps
   EXPECT_EQ(registerFromSpan(235.0, 20.0, 235.0), 255);
}

TEST(TestRegisterFromSpan, floors_below_the_minimum_and_saturates_above_full_scale)
{
   EXPECT_EQ(registerFromSpan(0.0, 20.0, 235.0), 0);
   EXPECT_EQ(registerFromSpan(10.0, 20.0, 235.0), 0);
   EXPECT_EQ(registerFromSpan(300.0, 20.0, 235.0), 255);
}

TEST(TestRegisterFromSpan, rejects_what_has_no_register_value)
{
   EXPECT_FALSE(registerFromSpan(-1.0, 20.0, 235.0).has_value());
   EXPECT_FALSE(registerFromSpan(kNaN, 20.0, 235.0).has_value());
   EXPECT_FALSE(registerFromSpan(kInf, 20.0, 235.0).has_value());
   EXPECT_FALSE(registerFromSpan(50.0, 20.0, 20.0).has_value());
   EXPECT_FALSE(registerFromSpan(50.0, 235.0, 20.0).has_value());
   EXPECT_FALSE(registerFromSpan(50.0, kNaN, 235.0).has_value());
   EXPECT_FALSE(registerFromSpan(50.0, 20.0, kInf).has_value());
}

TEST(TestSpeedToRegister, the_manual_range_spans_the_whole_byte)
{
   EXPECT_EQ(speedToRegister(k2F85.minSpeed, k2F85), 0);
   EXPECT_EQ(speedToRegister(0.085, k2F85), 128); // 127.5 steps
   EXPECT_EQ(speedToRegister(k2F85.maxSpeed, k2F85), 255);
}

TEST(TestSpeedToRegister, floors_and_saturates_outside_the_range)
{
   EXPECT_EQ(speedToRegister(0.0, k2F85), 0);
   EXPECT_EQ(speedToRegister(0.3, k2F85), 255);
}

TEST(TestSpeedToRegister, rejects_what_has_no_register_value)
{
   EXPECT_FALSE(speedToRegister(-0.1, k2F85).has_value());
   EXPECT_FALSE(speedToRegister(kNaN, k2F85).has_value());

   DeviceProfile broken = k2F85;
   broken.minSpeed = 0.020;
   broken.maxSpeed = 0.020;
   EXPECT_FALSE(speedToRegister(0.1, broken).has_value());
}

TEST(TestOpeningToRegister, rounds_to_the_nearest_value)
{
   EXPECT_EQ(openingToRegister(0.020, k2F85), 177); // 176.59 steps
}

TEST(TestOpeningToRegister, clamps_controller_overshoot_to_the_position_ends)
{
   EXPECT_EQ(openingToRegister(k2F85.maxOpening + 1e-4, k2F85), k2F85.openPosition);
   EXPECT_EQ(openingToRegister(-1e-4, k2F85), k2F85.closedPosition);
   EXPECT_EQ(openingToRegister(0.100, k2F85), k2F85.openPosition);
}

TEST(TestOpeningToRegister, supports_a_profile_whose_minimum_position_is_not_zero)
{
   DeviceProfile offset = k2F85;
   offset.minOpening = 0.010; // e.g. fingers that never fully meet
   offset.maxOpening = 0.095;
   EXPECT_EQ(openingToRegister(offset.maxOpening, offset), offset.openPosition); // fully open
   EXPECT_EQ(openingToRegister(offset.minOpening, offset), offset.closedPosition); // fully closed
   EXPECT_EQ(openingToRegister(0.005, offset), offset.closedPosition); // below minOpening clamps closed
}

TEST(TestOpeningToRegister, rejects_what_has_no_register_value)
{
   EXPECT_FALSE(openingToRegister(kNaN, k2F85).has_value());
   EXPECT_FALSE(openingToRegister(kInf, k2F85).has_value());

   DeviceProfile noSpan = k2F85;
   noSpan.minOpening = 0.0;
   noSpan.maxOpening = 0.0;
   EXPECT_FALSE(openingToRegister(0.02, noSpan).has_value());

   DeviceProfile noBand = k2F85;
   noBand.openPosition = 3;
   noBand.closedPosition = 3;
   EXPECT_FALSE(openingToRegister(0.02, noBand).has_value());
}

TEST(TestOpeningFromRegister, the_register_band_ends_land_on_the_position_span)
{
   EXPECT_DOUBLE_EQ(openingFromRegister(k2F85.openPosition, k2F85).value(), k2F85.maxOpening);
   EXPECT_DOUBLE_EQ(openingFromRegister(k2F85.closedPosition, k2F85).value(), k2F85.minOpening);
   EXPECT_NEAR(openingFromRegister(177, k2F85).value(), 0.019846, 1e-6);
}

TEST(TestOpeningFromRegister, a_closed_gripper_settling_short_of_closed_register_reads_slightly_open)
{
   EXPECT_NEAR(openingFromRegister(228, k2F85).value(), 0.00075, 1e-5);
}

TEST(TestOpeningFromRegister, supports_a_profile_whose_minimum_position_is_not_zero)
{
   DeviceProfile offset = k2F85;
   offset.minOpening = 0.010;
   offset.maxOpening = 0.095;
   EXPECT_DOUBLE_EQ(openingFromRegister(offset.openPosition, offset).value(), offset.maxOpening); // fully open
   EXPECT_DOUBLE_EQ(openingFromRegister(offset.closedPosition, offset).value(), offset.minOpening); // fully closed
}

TEST(TestOpeningFromRegister, rejects_a_profile_with_no_register_band)
{
   DeviceProfile broken = k2F85;
   broken.openPosition = 3;
   broken.closedPosition = 3;
   EXPECT_FALSE(openingFromRegister(broken.openPosition, broken).has_value());
   EXPECT_FALSE(openingFromRegister(255, broken).has_value());
}

TEST(TestKnuckleAngleToRegister, the_sweep_ends_land_on_the_register_band)
{
   EXPECT_EQ(knuckleAngleToRegister(0.0, k2F85), k2F85.openPosition); // fully open
   EXPECT_EQ(knuckleAngleToRegister(k2F85.knuckleAngleRange(), k2F85), k2F85.closedPosition); // fully closed
}

TEST(TestKnuckleAngleToRegister, rounds_to_the_nearest_value)
{
   EXPECT_EQ(knuckleAngleToRegister(k2F85.knuckleAngleRange() / 2.0, k2F85), 117); // 116.5 steps
}

TEST(TestKnuckleAngleToRegister, clamps_controller_overshoot_to_the_sweep_ends)
{
   EXPECT_EQ(knuckleAngleToRegister(-1e-4, k2F85), k2F85.openPosition);
   EXPECT_EQ(knuckleAngleToRegister(k2F85.knuckleAngleRange() + 1e-4, k2F85), k2F85.closedPosition);
   EXPECT_EQ(knuckleAngleToRegister(2.0, k2F85), k2F85.closedPosition);
}

TEST(TestKnuckleAngleToRegister, rejects_what_has_no_register_value)
{
   EXPECT_FALSE(knuckleAngleToRegister(kNaN, k2F85).has_value());
   EXPECT_FALSE(knuckleAngleToRegister(kInf, k2F85).has_value());

   DeviceProfile noBand = k2F85;
   noBand.openPosition = 3;
   noBand.closedPosition = 3;
   EXPECT_FALSE(knuckleAngleToRegister(0.4, noBand).has_value());
}

TEST(TestKnuckleAngleFromRegister, the_register_band_ends_land_on_the_sweep)
{
   EXPECT_DOUBLE_EQ(knuckleAngleFromRegister(k2F85.openPosition, k2F85).value(), 0.0);
   EXPECT_DOUBLE_EQ(knuckleAngleFromRegister(k2F85.closedPosition, k2F85).value(), k2F85.knuckleAngleRange());
}

TEST(TestKnuckleAngleFromRegister, values_outside_the_band_read_as_the_sweep_ends)
{
   EXPECT_DOUBLE_EQ(knuckleAngleFromRegister(0, k2F85).value(), 0.0);
   EXPECT_DOUBLE_EQ(knuckleAngleFromRegister(255, k2F85).value(), k2F85.knuckleAngleRange());
}

TEST(TestKnuckleAngleFromRegister, closes_the_opposite_way_round_to_the_opening)
{
   EXPECT_GT(knuckleAngleFromRegister(200, k2F85).value(), knuckleAngleFromRegister(100, k2F85).value());
   EXPECT_LT(openingFromRegister(200, k2F85).value(), openingFromRegister(100, k2F85).value());
}

TEST(TestKnuckleAngleFromRegister, inside_the_band_an_angle_read_back_commands_the_same_value)
{
   for(int count = k2F85.openPosition; count <= k2F85.closedPosition; ++count)
   {
      const auto raw = static_cast<uint8_t>(count);
      EXPECT_EQ(knuckleAngleToRegister(knuckleAngleFromRegister(raw, k2F85).value(), k2F85), raw) << "count " << count;
   }
}

TEST(TestKnuckleAngle, the_slope_survives_fingertips_that_meet_early)
{
   DeviceProfile thickTips = k2F85;
   thickTips.closedPosition = 200;
   EXPECT_DOUBLE_EQ(knuckleAngleFromRegister(thickTips.closedPosition, thickTips).value(),
                    k2F85.knuckleAnglePerCount * (thickTips.closedPosition - k2F85.openPosition));
   EXPECT_NEAR(knuckleAngleFromRegister(thickTips.closedPosition, thickTips).value(), 0.688, 1e-3);
}

TEST(TestKnuckleAngle, a_gripper_with_no_knuckle_has_no_angle_either_way)
{
   EXPECT_FALSE(knuckleAngleFromRegister(100, kHandE).has_value());
   EXPECT_FALSE(knuckleAngleToRegister(0.0, kHandE).has_value());
}

TEST(TestEffortToRegister, the_unit_span_maps_onto_the_whole_byte)
{
   EXPECT_EQ(effortToRegister(0.0), 0);
   EXPECT_EQ(effortToRegister(0.5), 128); // 127.5 steps
   EXPECT_EQ(effortToRegister(1.0), 255);
}

TEST(TestEffortToRegister, saturates_above_one)
{
   EXPECT_EQ(effortToRegister(1.5), 255);
}

TEST(TestEffortToRegister, rejects_what_has_no_register_value)
{
   EXPECT_FALSE(effortToRegister(-0.1).has_value());
   EXPECT_FALSE(effortToRegister(kNaN).has_value());
   EXPECT_FALSE(effortToRegister(kInf).has_value());
}

TEST(TestMotorCurrentFromRegister, ten_milliamperes_per_step)
{
   EXPECT_DOUBLE_EQ(motorCurrentFromRegister(0), 0.0);
   EXPECT_DOUBLE_EQ(motorCurrentFromRegister(20), 0.2);
}
} // namespace Robotiq::test
