"""The whole script against a simulated hub: what gets flashed, and every case where nothing may be flashed."""

import shutil
import sys

import pytest

from conftest import BIN_DIR, bh, device_info, firmware_rows

FW_1 = str(BIN_DIR / "Master_hub_1.1.8.cyacd")
FW_2 = str(BIN_DIR / "Master_hub_2.2.0.cyacd")


def on_2_1_hub(hub):
    hub.version, hub.device_info = b"1.1.8\x00", None


def on_old_vid_pid_hub(hub):
    on_2_1_hub(hub)
    hub.old_vid_pid = True


def no_version_reply(hub):
    hub.version = None


def blank_eeprom(hub):
    hub.device_info = None


def no_revision_in_eeprom(hub):
    hub.device_info = device_info(hub_version=(0, 0, 0), finger_versions=((0, 0, 0), (0, 0, 0)))


def in_bootloader(hub):
    hub.start_in_bootloader()


def assert_flashed(hub, firmware):
    assert hub.rows == firmware_rows(firmware)
    assert hub.exited_bootloader


def assert_nothing_flashed(hub):
    assert not hub.rebooted
    assert not hub.rows


@pytest.mark.parametrize("setup, argv, typed, firmware", [
    (None, [], "", FW_2),
    (on_2_1_hub, [], "", FW_1),
    (on_old_vid_pid_hub, [], "", FW_1),
    (None, ["--latest"], "", FW_2),
    (None, ["--firmware", FW_2], "", FW_2),
    (on_2_1_hub, ["--firmware", FW_1], "", FW_1),
    (blank_eeprom, ["--firmware", FW_2, "--force"], "FORCE\n", FW_2),
    (no_version_reply, ["--firmware", FW_2, "--force"], "FORCE\n", FW_2),
    (None, ["--firmware", FW_1, "--force"], "FORCE\n", FW_1),
    (in_bootloader, ["--firmware", FW_2, "--force"], "FORCE\n", FW_2),
    (in_bootloader, ["--firmware", FW_1, "--force"], "FORCE\n", FW_1),
    (lambda hub: setattr(hub, "device_info", device_info(hub_version=(2, 3, 0))), [], "", FW_2),
], ids=["2.2 auto", "2.1 auto", "old VID/PID auto", "--latest", "matching --firmware 2.x", "matching --firmware 1.x",
        "blank EEPROM, --firmware after FORCE", "failed detection, --firmware after FORCE",
        "other family --firmware after FORCE", "in bootloader, 2.x after FORCE", "in bootloader, 1.x after FORCE",
        "2.3 auto"])
def test_flashes(hub, run, setup, argv, typed, firmware):
    if setup:
        setup(hub)

    code, err = run(*argv, typed=typed)

    assert code == 0, err
    assert "Device programmed successfully!" in err
    assert_flashed(hub, firmware)


@pytest.mark.parametrize("setup, argv, typed, message", [
    (None, ["--firmware", FW_1], "", "Firmware 1.1.8 is not made for hardware revision 2.2"),
    (on_2_1_hub, ["--firmware", FW_2], "", "Firmware 2.2.0 is not made for hardware revision 2.1"),
    (on_old_vid_pid_hub, ["--firmware", FW_2], "", "Firmware 2.2.0 is not made for hardware revision 2.1"),
    (None, ["--firmware", FW_1, "--force"], "no\n", "Aborted"),
    (None, ["--firmware", FW_1, "--force"], "", "Aborted"),
    (no_version_reply, [], "", "Unknown hardware revision"),
    (blank_eeprom, [], "", "Unknown hardware revision"),
    (no_revision_in_eeprom, [], "", "pass the firmware made for it with --firmware and --force"),
    (no_revision_in_eeprom, ["--firmware", FW_1], "",
     "Firmware 1.1.8 is from another family than the firmware the hub runs (2.2.0)"),
    (blank_eeprom, ["--firmware", FW_2], "", "The hardware revision is unknown"),
    (no_version_reply, ["--firmware", FW_2], "", "The hardware revision is unknown"),
    (lambda hub: setattr(hub, "device_info", device_info(hardware_type=0x04)), [], "", "unexpected device info"),
    (lambda hub: setattr(hub, "version", b"3.0.0\x00"), [], "", "Unknown firmware family 3.0.0"),
    (in_bootloader, [], "", "Unknown hardware revision"),
    (lambda hub: setattr(hub, "app_running", False), ["--firmware", FW_2], "", "The hardware revision is unknown"),
], ids=["1.x --firmware on 2.2", "2.x --firmware on 2.1", "2.x --firmware on old VID/PID",
        "FORCE not typed", "no input for FORCE", "no version reply", "blank EEPROM", "no hardware version in EEPROM",
        "other family than the running firmware, no revision in EEPROM",
        "--firmware over blank EEPROM without FORCE", "--firmware with unknown revision", "unexpected EEPROM",
        "unknown family", "in bootloader", "--firmware with no hub detected"])
def test_refuses_before_rebooting(hub, run, setup, argv, typed, message):
    if setup:
        setup(hub)

    code, err = run(*argv, typed=typed)

    assert code == 1
    assert message in err
    assert_nothing_flashed(hub)


def on_newer_2_x_firmware(hub):
    hub.version = b"2.3.1\x00"


@pytest.mark.parametrize("argv", [[], ["--latest"], ["--force"]], ids=["default", "--latest", "--force"])
def test_refuses_to_downgrade_on_its_own(hub, run, argv):
    on_newer_2_x_firmware(hub)

    code, err = run(*argv)

    assert code == 1
    assert "The hub runs firmware 2.3.1, newer than 2.2.0, the latest in bin/" in err
    assert "--firmware " + FW_2 in err
    assert_nothing_flashed(hub)


def test_downgrades_when_the_firmware_is_named(hub, run):
    on_newer_2_x_firmware(hub)

    code, err = run("--firmware", FW_2)

    assert code == 0, err
    assert "Downgrading from 2.3.1 to 2.2.0" in err
    assert_flashed(hub, FW_2)


def test_unknown_revision_lists_only_the_firmware_of_the_running_family(hub, run):
    no_revision_in_eeprom(hub)

    code, err = run()

    assert code == 1
    assert "2.2 and 2.3: " + FW_2 in err
    assert "1.1.8" not in err


def test_force_names_the_other_family_when_the_revision_is_unknown(hub, run):
    no_revision_in_eeprom(hub)

    code, err = run("--firmware", FW_1, "--force", typed="FORCE\n")

    assert code == 0, err
    assert "firmware 1.1.8 is from another family than the firmware the hub runs (2.2.0)" in err
    assert_flashed(hub, FW_1)


def test_refuses_to_downgrade_a_2_1_hub_on_its_own(hub, run):
    on_2_1_hub(hub)
    hub.version = b"1.2.0\x00"

    code, err = run()

    assert code == 1
    assert "The hub runs firmware 1.2.0, newer than 1.1.8, the latest in bin/" in err
    assert_nothing_flashed(hub)


def test_refuses_a_firmware_file_name_without_version(hub, run, tmp_path):
    firmware = tmp_path / "hub_firmware.cyacd"
    shutil.copy(FW_2, firmware)

    code, err = run("--firmware", str(firmware))

    assert code == 1
    assert "can't be read from the file name" in err
    assert_nothing_flashed(hub)


def test_refuses_a_missing_firmware_file_before_rebooting(hub, run, tmp_path):
    code, err = run("--firmware", str(tmp_path / "Master_hub_2.2.0.cyacd"))

    assert code == 1
    assert "Can't read firmware file" in err
    assert_nothing_flashed(hub)


def test_refuses_a_corrupt_firmware_file_before_rebooting(hub, run, tmp_path):
    firmware = tmp_path / "Master_hub_2.2.0.cyacd"
    firmware.write_text(open(FW_2).read()[:-40])

    code, err = run("--firmware", str(firmware))

    assert code == 1
    assert "Can't read firmware file" in err
    assert_nothing_flashed(hub)


def test_refuses_firmware_for_another_chip_before_rebooting(hub, run, tmp_path):
    lines = open(FW_2).read().splitlines(keepends=True)
    firmware = tmp_path / "Master_hub_2.2.0.cyacd"
    firmware.write_text("FF" + lines[0][2:] + "".join(lines[1:]))

    code, err = run("--firmware", str(firmware))

    assert code == 1
    assert "Firmware is for silicon 0xFF" in err
    assert_nothing_flashed(hub)


def test_bootloader_that_never_appears_names_the_firmware_to_flash(hub, run):
    hub.reset_delay = None

    code, err = run()

    assert code == 1
    assert "Bootloader not found after 30 seconds" in err
    assert "run again with --firmware {} --force.".format(FW_2) in err
    assert not hub.rows


@pytest.mark.parametrize("euid, command", [(1000, "sudo, with --firmware {} --force"),
                                           (0, "--firmware {} --force")])
def test_bootloader_that_cannot_be_opened_names_the_firmware_to_flash(hub, run, monkeypatch, euid, command):
    monkeypatch.setattr(bh.sys, "platform", "linux")
    monkeypatch.setattr(bh.os, "geteuid", lambda: euid, raising=False)
    hub.hid_open_error = True

    code, err = run()

    assert code == 1
    assert "open failed" in err
    assert "The hub stays in its bootloader: run again with {}.".format(command.format(FW_2)) in err
    assert not hub.rows


@pytest.mark.parametrize("command, message", [(0x38, "Unable to communicate with bootloader!"),
                                              (0x39, "Error while programming!")], ids=["enter", "program row"])
def test_bootloader_error_names_the_firmware_to_flash(hub, run, command, message):
    hub.failing_commands = {command}

    code, err = run()

    assert code == 1
    assert message in err
    assert "run again with --firmware {} --force.".format(FW_2) in err


def test_invalid_application_checksum_fails(hub, run):
    hub.app_checksum_valid = False

    code, err = run()

    assert code == 1
    assert "Application checksum is invalid" in err
    assert not hub.exited_bootloader


def test_bootloader_without_the_checksum_command_exits_without_claiming_success(hub, run):
    hub.checksum_command_built_in = False

    code, err = run()

    assert code == 0, err
    assert "Programmed, but this bootloader can't verify the application checksum." in err
    assert "If the hub doesn't come back, run again with --firmware {} --force.".format(FW_2) in err
    assert "successfully" not in err
    assert_flashed(hub, FW_2)


def test_corrupt_reply_to_the_checksum_command_fails(hub, run):
    hub.checksum_command_built_in = False
    hub.corrupt_replies = {0x31}

    code, err = run()

    assert code == 1
    assert "Application checksum is invalid" in err
    assert not hub.exited_bootloader


@pytest.mark.skipif(not sys.platform.startswith("linux"), reason="the root warning is Linux only")
@pytest.mark.parametrize("euid, warned", [(1000, True), (0, False)])
def test_warns_when_not_root_on_linux(hub, run, monkeypatch, euid, warned):
    monkeypatch.setattr(bh.os, "geteuid", lambda: euid)

    code, err = run()

    assert code == 0
    assert ("Not running as root" in err) == warned
