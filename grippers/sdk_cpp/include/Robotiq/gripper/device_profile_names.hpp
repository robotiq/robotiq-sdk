// Copyright (c) 2026 Robotiq, Inc.
//
// Licensed under the BSD-3-Clause license; see LICENSE for details.

// Generated from data/device_profiles.json by tools/generate_device_profiles.py. Do not edit.

#pragma once

#include <array>
#include <optional>
#include <string_view>

#include <Robotiq/gripper/device_profile.hpp>

namespace Robotiq::profiles {

//! \ingroup units
//! \brief A shipped profile and its stable name.
struct NamedProfile
{
   std::string_view name; //!< Stable name: the profile's name in data/device_profiles.json
   DeviceProfile profile; //!< The profile with that name
};

//! \ingroup units
//! \brief Every shipped profile with its stable name, in data/device_profiles.json order.
inline constexpr std::array<NamedProfile, 5> kNamed{{
   {"2f_85", k2F85},
   {"2f_140", k2F140},
   {"hand_e", kHandE},
   {"hand_e_100", kHandE100},
   {"hand_e_c10", kHandEC10},
}};

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

} // namespace Robotiq::profiles
