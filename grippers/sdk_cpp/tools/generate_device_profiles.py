#!/usr/bin/env python3
"""Generate the device profiles C++ headers from device_profiles.json.

In device_profile.hpp, only the lines between the BEGIN and END marker
comments are rewritten. device_profile_names.hpp is written whole. Numbers
keep the exact text they have in the JSON file, so the C++ literals read
the same as the data.

Usage:
    generate_device_profiles.py [--check] <device_profiles.json>
        <device_profile.hpp> <device_profile_names.hpp>

Without --check, each header that changed is rewritten in place. With
--check, nothing is written and the exit status is 1 when a header is out
of date.
"""

import argparse
import json
import math
import re
import sys
from pathlib import Path

BEGIN_MARKER = "// BEGIN generated from data/device_profiles.json by tools/generate_device_profiles.py"
END_MARKER = "// END generated"
SCHEMA_VERSION = 1
NUMBER_FIELDS = (
    "min_speed",
    "max_speed",
    "min_opening",
    "max_opening",
    "knuckle_closed_angle",
)
REGISTER_FIELDS = ("open_position", "closed_position")
IDENTIFIER_FIELDS = {
    "name": re.compile(r"[a-z0-9_]+"),
    "constant": re.compile(r"k[A-Z0-9][A-Za-z0-9_]*"),
}
PROFILE_FIELDS = (*IDENTIFIER_FIELDS, *NUMBER_FIELDS, *REGISTER_FIELDS, "doc")
FILE_KEYS = ("schema_version", "profiles")
REGISTER_MAX = 255
REGISTER_TEXT = re.compile(r"[0-9]+")
LINE_BREAK_OR_TAB = re.compile(r"[\n\r\t]")
COLUMN_LIMIT = 120
NAMES_HEADER_TOP = """\
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

//! \\ingroup units
//! \\brief A shipped profile and its stable name.
struct NamedProfile
{
   std::string_view name; //!< Stable name: the profile's name in data/device_profiles.json
   DeviceProfile profile; //!< The profile with that name
};

//! \\ingroup units
//! \\brief Every shipped profile with its stable name, in data/device_profiles.json order.
"""
NAMES_HEADER_BOTTOM = """\
}};

//! \\ingroup units
//! \\brief The shipped profile called \\p name, or std::nullopt if there is none.
//! \\param name A stable name such as "hand_e_c10", matched exactly, case included.
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
"""


class IntegerText(str):
    pass


class RealText(str):
    pass


class ProfileError(Exception):
    pass


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--check", action="store_true", help="fail when a header is out of date"
    )
    parser.add_argument("json_path", type=Path)
    parser.add_argument("header_path", type=Path)
    parser.add_argument("names_path", type=Path)
    args = parser.parse_args()

    try:
        return generate(args)
    except (ProfileError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


def generate(args):
    profiles = load_profiles(args.json_path)
    header = read_utf8(args.header_path)
    names_missing = not args.check and not args.names_path.exists()
    names = "" if names_missing else read_utf8(args.names_path)
    targets = (
        (
            args.header_path,
            header,
            replace_block(header, render(profiles), args.header_path),
        ),
        (args.names_path, names, render_names(profiles)),
    )
    stale = [
        (path, current, updated)
        for path, current, updated in targets
        if updated != current.replace("\r\n", "\n")
    ]

    for path, current, updated in stale:
        if args.check:
            print(
                f"{path} is out of date with {args.json_path}; "
                "run: cmake --build <build> --target update_device_profiles",
                file=sys.stderr,
            )
        else:
            newline = "\r\n" if "\r\n" in current else "\n"
            path.write_bytes(updated.replace("\n", newline).encode("utf-8"))
    return 1 if args.check and stale else 0


def read_utf8(path):
    try:
        return path.read_bytes().decode("utf-8")
    except UnicodeDecodeError as error:
        raise ProfileError(f"{path}: is not UTF-8 text ({error})") from error


def load_profiles(path):
    try:
        data = json.loads(
            read_utf8(path),
            parse_int=IntegerText,
            parse_float=RealText,
            object_pairs_hook=lambda pairs: without_repeated_keys(pairs, path),
        )
    except json.JSONDecodeError as error:
        raise ProfileError(f"{path}: {error}") from error
    if not isinstance(data, dict):
        raise ProfileError(f"{path}: must be a JSON object")
    check_schema_version(data.get("schema_version"), path)
    for key in data:
        if key not in FILE_KEYS:
            raise ProfileError(f"{path}: has unknown key {key}")

    profiles = data.get("profiles")
    if not isinstance(profiles, list) or not profiles:
        raise ProfileError(f"{path}: needs a non-empty 'profiles' list")

    for index, profile in enumerate(profiles):
        check_profile(profile, f"{path}: {profile_label(profile, index)}")
    for field in IDENTIFIER_FIELDS:
        check_unique(profiles, field, path)
    return profiles


def without_repeated_keys(pairs, path):
    members = {}
    for key, value in pairs:
        if key in members:
            raise ProfileError(f"{path}: {key} appears more than once in an object")
        members[key] = value
    return members


def check_schema_version(version, path):
    if not isinstance(version, IntegerText) or int(version) != SCHEMA_VERSION:
        raise ProfileError(
            f"{path}: schema_version must be {SCHEMA_VERSION}, got {describe(version)}"
        )


def profile_label(profile, index):
    label = profile.get("name") if isinstance(profile, dict) else None
    return label if isinstance(label, str) else f"profile #{index}"


def check_unique(profiles, field, path):
    seen = set()
    for profile in profiles:
        if profile[field] in seen:
            raise ProfileError(
                f"{path}: {profile['name']}: {field} is used by another profile"
            )
        seen.add(profile[field])


def check_profile(profile, where):
    if not isinstance(profile, dict):
        raise ProfileError(f"{where}: must be an object")
    for field in PROFILE_FIELDS:
        if field not in profile:
            raise ProfileError(f"{where}: has no {field}")
    for field in profile:
        if field not in PROFILE_FIELDS:
            raise ProfileError(f"{where}: has unknown field {field}")
    for field, pattern in IDENTIFIER_FIELDS.items():
        check_identifier(profile[field], pattern, f"{where}: {field}")
    for field in NUMBER_FIELDS:
        check_number(profile[field], f"{where}: {field}")
    for field in REGISTER_FIELDS:
        check_register(profile[field], f"{where}: {field}")
    check_order(profile, ("min_speed", "max_speed"), where)
    check_order(profile, ("min_opening", "max_opening"), where)
    check_band(profile["open_position"], profile["closed_position"], where)
    check_doc(profile["doc"], f"{where}: doc")


def check_identifier(value, pattern, where):
    if type(value) is not str or not pattern.fullmatch(value):
        raise ProfileError(
            f"{where} must match {pattern.pattern}, got {describe(value)}"
        )


def check_number(value, where):
    if not isinstance(value, (IntegerText, RealText)):
        raise ProfileError(f"{where} must be a number, got {describe(value)}")
    if not math.isfinite(float(value)):
        raise ProfileError(f"{where} must be finite, got {value}")
    if float(value) < 0.0:
        raise ProfileError(f"{where} must be 0 or more, got {value}")


def check_order(profile, fields, where):
    low_field, high_field = fields
    if float(profile[low_field]) < float(profile[high_field]):
        return
    raise ProfileError(
        f"{where}: {low_field} ({profile[low_field]}) must be below "
        f"{high_field} ({profile[high_field]})"
    )


def check_register(value, where):
    is_register = isinstance(value, IntegerText) and REGISTER_TEXT.fullmatch(value)
    if not is_register or int(value) > REGISTER_MAX:
        raise ProfileError(
            f"{where} must be a register value from 0 to {REGISTER_MAX}, "
            f"got {describe(value)}"
        )


def check_band(open_position, closed_position, where):
    if int(closed_position) <= int(open_position):
        raise ProfileError(
            f"{where}: closed_position ({closed_position}) must be above "
            f"open_position ({open_position})"
        )


def check_doc(doc, where):
    if not isinstance(doc, list) or not doc or not doc[0]:
        raise ProfileError(f"{where} must be a list of lines starting with the brief")
    for line in doc:
        if type(line) is not str or not is_one_clean_line(line):
            raise ProfileError(
                f"{where} lines must be strings without line breaks or tabs, "
                "not ending with a space or a backslash"
            )


def is_one_clean_line(line):
    return (
        line == line.rstrip()
        and not line.endswith("\\")
        and not LINE_BREAK_OR_TAB.search(line)
    )


def describe(value):
    return value if isinstance(value, (IntegerText, RealText)) else json.dumps(value)


def render(profiles):
    lines = [BEGIN_MARKER]
    for profile in profiles:
        lines += checked_width(render_profile(profile), profile["name"])
    lines[-1] = END_MARKER
    return lines


def render_names(profiles):
    lines = [f"inline constexpr std::array<NamedProfile, {len(profiles)}> kNamed{{{{"]
    for profile in profiles:
        entry = f'   {{"{profile["name"]}", {profile["constant"]}}},'
        lines += checked_width([entry], profile["name"])
    return NAMES_HEADER_TOP + "\n".join(lines) + "\n" + NAMES_HEADER_BOTTOM


def render_profile(profile):
    lines = ["//! \\ingroup units", f"//! \\brief {profile['doc'][0]}"]
    lines += [f"//! {line}" if line else "//!" for line in profile["doc"][1:]]
    values = [
        profile[field]
        for field in ("min_speed", "max_speed", "min_opening", "max_opening")
    ]
    values += [
        profile["open_position"],
        profile["closed_position"],
        knuckle_slope(profile),
    ]
    lines += [
        f"inline constexpr DeviceProfile {profile['constant']}{{{', '.join(values)}}};",
        "",
    ]
    return lines


def checked_width(lines, name):
    for line in lines:
        if len(line) > COLUMN_LIMIT:
            raise ProfileError(
                f"{name}: a generated line is longer than {COLUMN_LIMIT} columns "
                f"(clang-format's ColumnLimit): {line}"
            )
    return lines


def knuckle_slope(profile):
    angle = profile["knuckle_closed_angle"]
    if float(angle) == 0.0:
        return "0.0"
    band = int(profile["closed_position"]) - int(profile["open_position"])
    return f"{angle} / {band}.0"


def replace_block(header, block, path):
    lines = header.replace("\r\n", "\n").split("\n")
    markers_once = lines.count(BEGIN_MARKER) == 1 and lines.count(END_MARKER) == 1
    if not markers_once or lines.index(END_MARKER) < lines.index(BEGIN_MARKER):
        raise ProfileError(
            f"{path}: needs exactly one line '{BEGIN_MARKER}' "
            f"and, after it, exactly one line '{END_MARKER}'"
        )
    begin, end = lines.index(BEGIN_MARKER), lines.index(END_MARKER)
    return "\n".join(lines[:begin] + block + lines[end + 1 :])


if __name__ == "__main__":
    sys.exit(main())
