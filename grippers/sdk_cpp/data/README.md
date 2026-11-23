# Device profiles data file

`device_profiles.json` holds the gripper profiles the SDK ships: the speed
range, the opening range and the measured register band of each model. The
C++ constants in `include/Robotiq/gripper/device_profile.hpp`
(`profiles::k2F85` and the others) and the lookup by name in
`include/Robotiq/gripper/device_profile_names.hpp` are generated from it,
so a program in another language that reads this file uses the same values
as the C++ SDK instead of a copy.

## Where to find it

The file installs with the SDK at `share/grippers/device_profiles.json`
under the install prefix. After `find_package(grippers)`, the CMake variable
`grippers_DEVICE_PROFILES` holds its absolute path. The variable is set by
`find_package` only: a project that brings the SDK in with
`add_subdirectory()` or FetchContent reads the file from the source tree,
at `sdk_cpp/data/device_profiles.json`.

## What it holds

```json
{
  "schema_version": 1,
  "profiles": [
    {
      "name": "2f_85",
      "min_speed": 0.020,
      "max_speed": 0.150,
      "min_opening": 0.0,
      "max_opening": 0.085,
      "open_position": 3,
      "closed_position": 230,
      "knuckle_closed_angle": 0.7929
    }
  ]
}
```

| Field | Unit | Meaning |
| --- | --- | --- |
| `name` | | Stable name of the profile, the one `profiles::profileNamed()` takes. |
| `min_speed`, `max_speed` | m/s | Finger speed at speed register 0 and 255. |
| `min_opening`, `max_opening` | m | Opening at `closed_position` and at `open_position`. |
| `open_position`, `closed_position` | counts, 0 to 255 | Position register read back fully open and fully closed, in parallel-finger mode. |
| `knuckle_closed_angle` | rad | Knuckle angle at `closed_position`; the angle is 0 rad at `open_position`. `0.0` when the model has no knuckle. |

The knuckle angle grows linearly between the two ends of the register band,
so one count is `knuckle_closed_angle / (closed_position - open_position)`
rad. That slope is what C++ stores as `DeviceProfile::knuckleAnglePerCount`;
dividing by `closed_position` alone gives a wrong slope.

Each profile also has a `constant` (its C++ name) and a `doc` (its Doxygen
comment). They serve the generated header only.

## What readers can rely on

The contract for programs in other languages is each profile's `name` and
the numeric fields of the table. It is part of the documented API the
[versioning policy](../../README.md#versioning) covers. `schema_version`
changes when that contract changes, so a reader should check it and refuse
a version it does not know.

## Changing a profile

Edit the JSON, regenerate the headers and commit them with it: see
[generate_device_profiles.py](../tools/README.md#generate_device_profilespy).
