#!/usr/bin/env python3
"""Generate the device profiles block of device_profile.hpp from device_profiles.json.

Only the lines between the BEGIN and END marker comments of the header are
rewritten. Numbers keep the exact text they have in the JSON file, so the
C++ literals read the same as the data.

Usage:
    generate_device_profiles.py [--check] <device_profiles.json> <device_profile.hpp>

Without --check, the header is rewritten in place when its block changed.
With --check, nothing is written and the exit status is 1 when the header
is out of date.
"""

import argparse
import json
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
    "constant": re.compile(r"[A-Za-z_][A-Za-z0-9_]*"),
}
REGISTER_MAX = 255
REGISTER_TEXT = re.compile(r"0|[1-9][0-9]*")
LINE_BREAK = re.compile(r"[\n\r]")
COLUMN_LIMIT = 120


class IntegerText(str):
    pass


class RealText(str):
    pass


class ProfileError(Exception):
    pass


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--check", action="store_true", help="fail when the header is out of date"
    )
    parser.add_argument("json_path", type=Path)
    parser.add_argument("header_path", type=Path)
    args = parser.parse_args()

    try:
        block = render(load_profiles(args.json_path))
        current = args.header_path.read_bytes().decode("utf-8")
        updated = replace_block(current, block, args.header_path)
    except ProfileError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    if updated == current.replace("\r\n", "\n"):
        return 0
    if args.check:
        print(
            f"{args.header_path} is out of date with {args.json_path}; "
            "run: cmake --build <build> --target update_device_profiles",
            file=sys.stderr,
        )
        return 1

    newline = "\r\n" if "\r\n" in current else "\n"
    args.header_path.write_bytes(updated.replace("\n", newline).encode("utf-8"))
    return 0


def load_profiles(path):
    try:
        data = json.loads(
            path.read_text(encoding="utf-8"),
            parse_int=IntegerText,
            parse_float=RealText,
        )
    except json.JSONDecodeError as error:
        raise ProfileError(f"{path}: {error}") from error
    if not isinstance(data, dict):
        raise ProfileError(f"{path}: must be a JSON object")
    check_schema_version(data.get("schema_version"), path)

    profiles = data.get("profiles")
    if not isinstance(profiles, list) or not profiles:
        raise ProfileError(f"{path}: needs a non-empty 'profiles' list")

    for index, profile in enumerate(profiles):
        check_profile(profile, f"{path}: {profile_label(profile, index)}")
    for field in IDENTIFIER_FIELDS:
        check_unique(profiles, field, path)
    return profiles


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
    for field in (*IDENTIFIER_FIELDS, *NUMBER_FIELDS, *REGISTER_FIELDS, "doc"):
        if field not in profile:
            raise ProfileError(f"{where}: has no {field}")
    for field, pattern in IDENTIFIER_FIELDS.items():
        check_identifier(profile[field], pattern, f"{where}: {field}")
    for field in NUMBER_FIELDS:
        check_number(profile[field], f"{where}: {field}")
    for field in REGISTER_FIELDS:
        check_register(profile[field], f"{where}: {field}")
    check_order(profile, ("min_speed", "max_speed"), where, strict=False)
    check_order(profile, ("min_opening", "max_opening"), where, strict=True)
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
    if float(value) < 0.0:
        raise ProfileError(f"{where} must be 0 or more, got {value}")


def check_order(profile, fields, where, strict):
    low_field, high_field = fields
    low, high = float(profile[low_field]), float(profile[high_field])
    if low < high or (low == high and not strict):
        return
    relation = "below" if strict else "at most"
    raise ProfileError(
        f"{where}: {low_field} ({profile[low_field]}) must be {relation} "
        f"{high_field} ({profile[high_field]})"
    )


def check_register(value, where):
    is_register = isinstance(value, IntegerText) and REGISTER_TEXT.fullmatch(value)
    if not is_register or int(value) > REGISTER_MAX:
        raise ProfileError(
            f"{where} must be a register value from 0 to {REGISTER_MAX} "
            f"without leading zeros, got {describe(value)}"
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
        if type(line) is not str or line.endswith("\\") or LINE_BREAK.search(line):
            raise ProfileError(
                f"{where} lines must be strings without line breaks "
                "and not ending with a backslash"
            )


def describe(value):
    return value if isinstance(value, (IntegerText, RealText)) else json.dumps(value)


def render(profiles):
    lines = [BEGIN_MARKER]
    for profile in profiles:
        lines += checked_width(render_profile(profile), profile["name"])

    lines += [
        "//! \\ingroup units",
        "//! \\brief Every shipped profile with its stable name, in data/device_profiles.json order.",
        f"inline constexpr std::array<NamedProfile, {len(profiles)}> kNamed{{{{",
    ]
    for profile in profiles:
        entry = f'   {{"{profile["name"]}", {profile["constant"]}}},'
        lines += checked_width([entry], profile["name"])
    lines += ["}};", END_MARKER]
    return "\n".join(lines) + "\n"


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
    text = header.replace("\r\n", "\n")
    begin = text.find(BEGIN_MARKER + "\n")
    end = text.find(END_MARKER + "\n", begin)
    if begin < 0 or end < 0:
        raise ProfileError(
            f"{path}: needs the lines '{BEGIN_MARKER}' and '{END_MARKER}'"
        )
    return text[:begin] + block + text[end + len(END_MARKER) + 1 :]


if __name__ == "__main__":
    sys.exit(main())
