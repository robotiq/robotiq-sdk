// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Robotiq {

//! \ingroup units
//! \brief Gripper model specific profile gathering specifications such as
//!        opening range and speed.
//!
//! openPosition and closedPosition are measured, not taken from the
//! manual: with nothing between the fingers, command rPR 0 and read gPO
//! back, then command rPR 255 and read gPO back. Do this on your own
//! gripper when filling a new profile, especially with custom fingers.
//!
//! \warning On the 2F grippers, position <-> register conversion are
//!          valid only while the gripper stays in parallel-finger mode.
//!          In encompassing mode gPO can go past closedPosition; this
//!          profile has no way to represent that, so a reading taken in
//!          encompassing mode does not convert to a meaningful opening.
//!          The Hand-E family has no encompassing mode.
//!
//! \warning minOpening, maxOpening, openPosition and closedPosition
//!          depend on which fingers are installed on the gripper:
//!          swapping fingers changes the physical opening range, and
//!          can shift the measured register endpoints too.
struct DeviceProfile
{
   double minSpeed; //!< m/s
   double maxSpeed; //!< m/s
   double minOpening; //!< m — opening at closedPosition
   double maxOpening; //!< m — opening at openPosition
   uint8_t openPosition; //!< gPO after commanding rPR 0, measured
   uint8_t closedPosition; //!< gPO after commanding rPR 255, measured

   //! rad/count of the 2F knuckle; must be positive, 0 when there is no knuckle.
   double knuckleAnglePerCount = 0.0;

   //! Register steps from full opening to full closure, in parallel mode.
   [[nodiscard]] constexpr double registerPositionRange() const
   {
      return static_cast<double>(closedPosition) - openPosition;
   }

   //! Metres from full closure to full opening.
   [[nodiscard]] constexpr double openingRange() const { return maxOpening - minOpening; }

   //! Radians the knuckle sweeps over the whole register band.
   [[nodiscard]] constexpr double knuckleAngleRange() const { return knuckleAnglePerCount * registerPositionRange(); }
};

namespace profiles {
//! \ingroup units
//! \brief A shipped profile and its stable name.
struct NamedProfile
{
   std::string_view name; //!< Stable name, as robotiq-ros's gripper_profile spells it
   DeviceProfile profile; //!< The profile with that name
};

// BEGIN generated from data/device_profiles.json by tools/generate_device_profiles.py
//! \ingroup units
//! \brief The measured profile for the 2F-85
//!
//! \warning Assumes that the gripper is in parallel-finger mode.
inline constexpr DeviceProfile k2F85{0.020, 0.150, 0.0, 0.085, 3, 230, 0.7929 / 227.0};

//! \ingroup units
//! \brief The measured profile for the 2F-140
//!
//! \warning Assumes that the gripper is in parallel-finger mode.
inline constexpr DeviceProfile k2F140{0.030, 0.250, 0.0, 0.140, 3, 230, 0.77 / 227.0};

//! \ingroup units
//! \brief The measured profile for the Hand-E
inline constexpr DeviceProfile kHandE{0.020, 0.150, 0.0, 0.050, 3, 250, 0.0};

//! \ingroup units
//! \brief The measured profile for the Hand-E 100 (100 mm stroke)
//!
//! Same speed range as the Hand-E (product sheet: 20 to 150 mm/s) over twice
//! the stroke. Register endpoints measured on a Hand-E 100: 3 and 250, the
//! Hand-E's band, so one count is about 0.4 mm of opening.
inline constexpr DeviceProfile kHandE100{0.020, 0.150, 0.0, 0.100, 3, 250, 0.0};

//! \ingroup units
//! \brief The measured profile for the Hand-E C10
//!
//! Opening is the diameter between the three fingertips.
inline constexpr DeviceProfile kHandEC10{0.014, 0.107, 0.0, 0.040, 3, 250, 0.0};

//! \ingroup units
//! \brief Every shipped profile with its stable name, in data/device_profiles.json order.
inline constexpr std::array<NamedProfile, 5> kNamed{{
   {"2f_85", k2F85},
   {"2f_140", k2F140},
   {"hand_e", kHandE},
   {"hand_e_100", kHandE100},
   {"hand_e_c10", kHandEC10},
}};
// END generated

//! \ingroup units
//! \brief The shipped profile called \p name, or std::nullopt if there is none.
//! \param name A stable name such as "hand_e_c10", matched exactly, case included.
[[nodiscard]] constexpr std::optional<DeviceProfile> profileNamed(std::string_view name)
{
   for(const NamedProfile& entry : kNamed)
   {
      if(entry.name == name)
      {
         return entry.profile;
      }
   }
   return std::nullopt;
}

} // namespace profiles

} // namespace Robotiq
