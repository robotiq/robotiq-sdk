#!/usr/bin/env python3
"""Tests of generate_device_profiles.py, run on copies of the shipped files.

Usage:
    test_generate_device_profiles.py
"""

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
SCRIPT = TOOLS / "generate_device_profiles.py"
SHIPPED_JSON = TOOLS.parent / "data" / "device_profiles.json"
HEADERS = TOOLS.parent / "include" / "Robotiq" / "gripper"
SHIPPED_HEADER = HEADERS / "device_profile.hpp"
SHIPPED_NAMES_HEADER = HEADERS / "device_profile_names.hpp"
END_LINE = "// END generated\n"
BRIEF = '"The measured profile for the 2F-85"'
REJECTED = (
    (
        "another schema version",
        '"schema_version": 1',
        '"schema_version": 2',
        "schema_version must be 1",
    ),
    (
        "an upper-case name",
        '"name": "2f_85"',
        '"name": "2F_85"',
        "2F_85: name must match",
    ),
    (
        "a keyword as constant",
        '"constant": "k2F85"',
        '"constant": "int"',
        "2f_85: constant must match",
    ),
    (
        "a repeated name",
        '"name": "2f_140"',
        '"name": "2f_85"',
        "name is used by another profile",
    ),
    (
        "a repeated constant",
        '"constant": "k2F140"',
        '"constant": "k2F85"',
        "constant is used by another profile",
    ),
    ("a missing field", '"min_speed": 0.020,', "", "2f_85: has no min_speed"),
    (
        "an unknown field",
        '"min_speed": 0.020,',
        '"min_speed": 0.020, "payload": 1,',
        "2f_85: has unknown field payload",
    ),
    (
        "an unknown top-level key",
        '"schema_version": 1,',
        '"schema_version": 1, "extra": 1,',
        "has unknown key extra",
    ),
    (
        "a repeated key",
        '"min_speed": 0.020,',
        '"min_speed": 0.020, "min_speed": 0.021,',
        "min_speed appears more than once",
    ),
    (
        "a negative speed",
        '"min_speed": 0.020',
        '"min_speed": -0.020',
        "min_speed must be 0 or more",
    ),
    (
        "an infinite speed",
        '"max_speed": 0.150',
        '"max_speed": 1e400',
        "max_speed must be finite",
    ),
    (
        "a speed written as text",
        '"min_speed": 0.020',
        '"min_speed": "0.020"',
        "min_speed must be a number",
    ),
    (
        "equal speeds",
        '"min_speed": 0.020',
        '"min_speed": 0.150',
        "min_speed (0.150) must be below max_speed",
    ),
    (
        "equal openings",
        '"min_opening": 0.0',
        '"min_opening": 0.085',
        "min_opening (0.085) must be below max_opening",
    ),
    (
        "a register above 255",
        '"open_position": 3',
        '"open_position": 300',
        "open_position must be a register value",
    ),
    (
        "a fractional register",
        '"open_position": 3',
        '"open_position": 3.5',
        "open_position must be a register value",
    ),
    (
        "a negative register",
        '"open_position": 3',
        '"open_position": -3',
        "open_position must be a register value",
    ),
    (
        "an empty register band",
        '"closed_position": 230',
        '"closed_position": 3',
        "closed_position (3) must be above",
    ),
    ("an empty brief", BRIEF, '""', "2f_85: doc must be a list of lines"),
    ("a doc line ending with a space", BRIEF, BRIEF[:-1] + ' "', "2f_85: doc lines"),
    ("a doc line with a tab", BRIEF, BRIEF.replace(" 2F", "\\t2F"), "2f_85: doc lines"),
    (
        "a doc line with a line break",
        BRIEF,
        BRIEF.replace(" 2F", "\\n2F"),
        "2f_85: doc lines",
    ),
    (
        "a doc line ending with a backslash",
        BRIEF,
        BRIEF[:-1] + '\\\\"',
        "2f_85: doc lines",
    ),
    (
        "a doc line over the column limit",
        BRIEF,
        '"' + "x" * 130 + '"',
        "longer than 120 columns",
    ),
    (
        "a name too long for its table line",
        '"name": "2f_85"',
        '"name": "' + "x" * 130 + '"',
        "longer than 120 columns",
    ),
)


def with_unix_line_endings(path):
    return path.read_bytes().replace(b"\r\n", b"\n")


class GeneratorTest(unittest.TestCase):
    def setUp(self):
        self.directory = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.directory)
        self.json_path = self.directory / "device_profiles.json"
        self.header_path = self.directory / "device_profile.hpp"
        self.json_path.write_bytes(with_unix_line_endings(SHIPPED_JSON))
        self.names_path = self.directory / "device_profile_names.hpp"
        self.header_path.write_bytes(with_unix_line_endings(SHIPPED_HEADER))
        self.names_path.write_bytes(with_unix_line_endings(SHIPPED_NAMES_HEADER))

    def generate(self, *flags):
        command = [
            sys.executable,
            str(SCRIPT),
            *flags,
            str(self.json_path),
            str(self.header_path),
            str(self.names_path),
        ]
        return subprocess.run(command, capture_output=True, text=True, check=False)

    def edit(self, path, old, new):
        text = path.read_bytes().decode("utf-8")
        self.assertIn(old, text)
        path.write_bytes(text.replace(old, new, 1).encode("utf-8"))

    def rename_a_profile(self):
        self.edit(self.json_path, '"name": "hand_e_c10"', '"name": "hand_e_c12"')

    def header_text(self):
        return self.header_path.read_bytes().decode("utf-8")

    def names_text(self):
        return self.names_path.read_bytes().decode("utf-8")

    def assert_rejected(self, result, reason):
        self.assertEqual(result.returncode, 1)
        self.assertTrue(result.stderr.startswith("error: "), result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertIn(reason, result.stderr)

    def test_the_shipped_headers_are_current(self):
        result = self.generate("--check")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_check_fails_on_a_stale_header_and_writes_nothing(self):
        self.edit(self.json_path, '"max_speed": 0.150', '"max_speed": 0.151')
        before = self.header_text()

        result = self.generate("--check")

        self.assertEqual(result.returncode, 1)
        self.assertIn("out of date", result.stderr)
        self.assertEqual(self.header_text(), before)

    def test_writing_brings_a_stale_header_up_to_date(self):
        self.edit(self.json_path, '"max_speed": 0.150', '"max_speed": 0.151')

        result = self.generate()

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("k2F85{0.020, 0.151,", self.header_text())
        self.assertEqual(self.generate("--check").returncode, 0)

    def test_text_outside_the_block_is_left_alone(self):
        self.edit(self.json_path, '"max_speed": 0.150', '"max_speed": 0.151')
        before = self.header_text()

        self.generate()

        after = self.header_text()
        self.assertEqual(
            after.split("// BEGIN generated")[0], before.split("// BEGIN generated")[0]
        )
        self.assertEqual(after.split(END_LINE)[1], before.split(END_LINE)[1])

    def test_a_header_with_windows_line_endings_is_current_and_keeps_them(self):
        self.header_path.write_bytes(
            self.header_text().replace("\n", "\r\n").encode("utf-8")
        )
        self.assertEqual(self.generate("--check").returncode, 0)
        self.edit(self.json_path, '"max_speed": 0.150', '"max_speed": 0.151')

        self.generate()

        self.assertNotIn("\n", self.header_text().replace("\r\n", ""))

    def test_a_doc_line_holding_the_end_marker_does_not_cut_the_block(self):
        self.edit(self.json_path, BRIEF, BRIEF[:-1] + ' // END generated"')
        self.generate()
        once = self.header_text()

        result = self.generate()

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.header_text(), once)
        self.assertEqual(once.count("\n" + END_LINE), 1)

    def test_a_header_without_its_end_marker_is_refused(self):
        self.edit(self.header_path, END_LINE, "")

        self.assert_rejected(self.generate("--check"), "needs exactly one line")

    def test_a_header_with_a_second_end_marker_is_refused(self):
        self.edit(self.header_path, END_LINE, END_LINE + END_LINE)

        self.assert_rejected(self.generate("--check"), "needs exactly one line")

    def test_a_header_with_its_markers_swapped_is_refused(self):
        begin_line = next(
            line
            for line in self.header_text().splitlines(True)
            if line.startswith("// BEGIN generated")
        )
        self.edit(self.header_path, begin_line, "")
        self.edit(self.header_path, END_LINE, END_LINE + begin_line)

        self.assert_rejected(self.generate("--check"), "needs exactly one line")

    def test_the_profile_header_holds_no_name_lookup(self):
        header = self.header_text()

        for name_side in (
            "kNamed",
            "NamedProfile",
            "profileNamed",
            "<array>",
            "<optional>",
            "<string_view>",
        ):
            self.assertNotIn(name_side, header)

    def test_the_names_header_lists_each_profile_in_data_file_order(self):
        data = json.loads(self.json_path.read_text(encoding="utf-8"))
        hand_e = next(p for p in data["profiles"] if p["name"] == "hand_e_c10")
        data["profiles"].remove(hand_e)
        data["profiles"].insert(0, hand_e)
        self.json_path.write_text(json.dumps(data, indent=2), encoding="utf-8")

        result = self.generate()

        self.assertEqual(result.returncode, 0, result.stderr)
        names = self.names_text()
        self.assertIn("std::array<NamedProfile, 5> kNamed{{", names)
        self.assertLess(
            names.index('{"hand_e_c10", kHandEC10},'), names.index('{"2f_85", k2F85},')
        )

    def test_check_fails_on_a_stale_names_header_and_writes_nothing(self):
        self.rename_a_profile()
        before = self.names_text()

        result = self.generate("--check")

        self.assertEqual(result.returncode, 1)
        self.assertIn("device_profile_names.hpp is out of date", result.stderr)
        self.assertNotIn("device_profile.hpp is out of date", result.stderr)
        self.assertEqual(self.names_text(), before)

    def test_writing_brings_a_stale_names_header_up_to_date(self):
        self.rename_a_profile()

        result = self.generate()

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('{"hand_e_c12", kHandEC10},', self.names_text())
        self.assertEqual(self.generate("--check").returncode, 0)

    def test_check_names_each_stale_file(self):
        self.edit(self.json_path, '"constant": "kHandEC10"', '"constant": "kHandEC12"')

        result = self.generate("--check")

        self.assertEqual(result.returncode, 1)
        self.assertIn("device_profile.hpp is out of date", result.stderr)
        self.assertIn("device_profile_names.hpp is out of date", result.stderr)

    def test_writing_brings_both_stale_headers_up_to_date(self):
        self.edit(self.json_path, '"constant": "kHandEC10"', '"constant": "kHandEC12"')

        result = self.generate()

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("kHandEC12{", self.header_text())
        self.assertIn('{"hand_e_c10", kHandEC12},', self.names_text())
        self.assertEqual(self.generate("--check").returncode, 0)

    def test_a_hand_edit_anywhere_in_the_names_header_is_overwritten(self):
        shipped = self.names_text()
        self.edit(self.names_path, "#pragma once", "#pragma once\n#include <string>")
        self.assertEqual(self.generate("--check").returncode, 1)

        self.generate()

        self.assertEqual(self.names_text(), shipped)

    def test_a_missing_names_header_is_written_whole(self):
        shipped = self.names_text()
        self.names_path.unlink()

        result = self.generate()

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.names_text(), shipped)

    def test_check_refuses_a_missing_names_header(self):
        self.names_path.unlink()

        self.assert_rejected(self.generate("--check"), "device_profile_names.hpp")

    def test_a_names_header_with_windows_line_endings_is_current_and_keeps_them(self):
        self.names_path.write_bytes(
            self.names_text().replace("\n", "\r\n").encode("utf-8")
        )
        self.assertEqual(self.generate("--check").returncode, 0)
        self.rename_a_profile()

        self.generate()

        self.assertIn("hand_e_c12", self.names_text())
        self.assertNotIn("\n", self.names_text().replace("\r\n", ""))

    def test_a_names_header_that_is_not_utf8_is_an_error_line(self):
        self.names_path.write_bytes(b"\xff\xfe")

        self.assert_rejected(
            self.generate("--check"), "device_profile_names.hpp: is not UTF-8 text"
        )

    def test_bad_data_is_refused_with_the_profile_and_field_named(self):
        shipped = with_unix_line_endings(SHIPPED_JSON)
        for case, old, new, reason in REJECTED:
            with self.subTest(case):
                self.json_path.write_bytes(shipped)
                self.edit(self.json_path, old, new)

                self.assert_rejected(self.generate("--check"), reason)

    def test_a_missing_data_file_is_an_error_line(self):
        self.json_path.unlink()

        self.assert_rejected(self.generate("--check"), "device_profiles.json")

    def test_a_header_that_is_not_utf8_is_an_error_line(self):
        self.header_path.write_bytes(b"\xff\xfe")

        self.assert_rejected(
            self.generate("--check"), "device_profile.hpp: is not UTF-8 text"
        )


if __name__ == "__main__":
    unittest.main()
