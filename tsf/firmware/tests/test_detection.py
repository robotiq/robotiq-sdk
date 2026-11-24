"""Talking to the hub application: the packet parser and the hardware revision detection."""

import pytest

from conftest import bh, device_info, device_info_row, stream_packet

# Read from hub TH100032: production wrote no hub version, its TSF85 fingers say 2.3.1
TH100032_DEVICE_INFO = bytes.fromhex("0103000000544831303030333207eab7"
                                     "0101020301545031323331333007eac7"
                                     "0101020301545031323330353307eacb")


def test_reply_is_found(serial_device):
    assert bh.request_usb_reply(serial_device, bh.USB_COMMAND_GET_FIRMWARE_VERSION) == b"2.2.0\x00"


def test_reply_is_found_behind_stream_packets(hub, serial_device):
    hub.before_reply = stream_packet() * 3

    assert bh.request_usb_reply(serial_device, bh.USB_COMMAND_GET_FIRMWARE_VERSION) == b"2.2.0\x00"


def test_reply_is_found_behind_a_cut_stream_packet_holding_a_start_byte(hub, serial_device):
    # The flush cut a stream packet: its tail holds 0x9A followed by a large "length" that would swallow the reply
    hub.flush_leftover = bytes([0x11, bh.USB_PACKET_START_BYTE, 0x00, 0x58, 0xFF, 0x01])

    assert bh.request_usb_reply(serial_device, bh.USB_COMMAND_GET_FIRMWARE_VERSION) == b"2.2.0\x00"


def test_reply_with_a_non_zero_crc_byte_is_found(hub, serial_device):
    # Firmware up to 1.1.3 sends whatever was on its stack as the CRC byte
    hub.reply_crc = 0xA7

    assert bh.request_usb_reply(serial_device, bh.USB_COMMAND_GET_FIRMWARE_VERSION) == b"2.2.0\x00"


def test_no_reply_times_out(hub, serial_device):
    hub.version = None

    assert bh.request_usb_reply(serial_device, bh.USB_COMMAND_GET_FIRMWARE_VERSION) is None


def test_drain_input_discards_what_is_waiting(hub, serial_device):
    serial_device.buffer += stream_packet() * 10

    bh.drain_input(serial_device)

    assert serial_device.in_waiting == 0


@pytest.mark.parametrize("info, status", [
    (device_info(), "valid"),
    (None, "blank"),
    (device_info()[:-1], "blank"),
    (device_info(valid_checksum=False), "blank"),
    (device_info(hardware_type=0x04), "inconsistent"),
    (device_info(serial_number=b"TP222015"), "inconsistent"),
], ids=["valid", "no reply", "short reply", "bad checksum", "not a hub type", "not a hub serial number"])
def test_read_hub_device_info(hub, serial_device, info, status):
    hub.device_info = info

    assert bh.read_hub_device_info(serial_device)[0] == status


@pytest.mark.parametrize("old_vid_pid, version, info, hw_rev", [
    (True, b"1.1.8\x00", None, "2.1"),
    (True, None, None, "2.1"),
    (False, b"1.1.8\x00", None, "2.1"),
    (False, b"2.2.0\x00", device_info(), "2.2"),
    (False, b"2.3.1\x00", device_info(hub_version=(2, 3, 0), finger_versions=((2, 3, 0), (2, 3, 0))), "2.3"),
    (False, b"2.3.1\x00", TH100032_DEVICE_INFO, "2.3"),
    (False, b"2.2.0\x00", device_info(hub_version=(0, 0, 0)), "2.2"),
    (False, b"2.2.0\x00", device_info(hub_version=(2, 3, 0)), "2.3"),
    (False, b"2.2.0\x00", device_info(hub_version=(2, 2, 0), finger_versions=((2, 1, 0), (2, 1, 0))), None),
    (False, b"2.2.0\x00", device_info(hub_version=(0, 0, 0), finger_versions=((0, 0, 0), (0, 0, 0))), None),
    (False, b"2.2.0\x00", device_info(hub_version=(0, 0, 0), finger_versions=((2, 2, 0), (2, 3, 0))), "2.3"),
    (False, b"2.2.0\x00", None, None),
    (False, b"2.2.0\x00", device_info(hardware_type=0x04), None),
    (False, b"2.2.0\x00", device_info(hub_version=(2, 4, 0), finger_versions=((2, 4, 0), (2, 4, 0))), "2.3"),
    (False, b"2.2.0\x00", device_info(hub_version=(3, 0, 0), finger_versions=((3, 0, 0), (3, 0, 0))), None),
    (False, b"2.2.0\x00", device_info(hub_version=(255, 7, 0), finger_versions=((0, 0, 0), (0, 0, 0))), None),
    (False, b"2.2.0\x00", device_info(hub_version=(0, 3, 0), finger_versions=((0, 0, 0), (0, 0, 0))), None),
    (False, b"2.2.0\x00", device_info(hub_version=(2, 0, 0)), None),
    (False, b"2.2.0\x00", device_info(hub_version=(2, 1, 0)), None),
    (False, None, None, None),
    (False, b"3.0.0\x00", None, None),
    (False, b"garbage\x00", None, None),
], ids=["old VID/PID", "old VID/PID, no version reply", "1.x", "hub says 2.2", "hub says 2.3", "TH100032",
        "fingers say 2.2", "hub and fingers differ within the family", "hub and fingers differ in family",
        "no version in EEPROM", "fingers differ within the family", "blank EEPROM", "unexpected EEPROM",
        "newer revision 2.4", "newer major 3.0", "garbage revision 255.7", "major 0 with a minor",
        "older unknown revision 2.0",
        "revision of the other family", "no version reply",
        "unknown family", "unparseable version"])
def test_detect_hardware_revision(hub, serial_device, old_vid_pid, version, info, hw_rev):
    hub.version, hub.device_info = version, info

    assert bh.detect_hardware_revision(serial_device, old_vid_pid)[0] == hw_rev


def test_detection_stops_the_sensor_stream_first(hub, serial_device):
    bh.detect_hardware_revision(serial_device, False)

    assert hub.serial_commands[0] == bh.USB_COMMAND_AUTOSEND_ASYNC_SENSORS


def test_detect_hardware_revision_returns_the_running_version(hub, serial_device):
    hub.version = b"2.3.1\x00"

    assert bh.detect_hardware_revision(serial_device, False) == ("2.2", (2, 3, 1))


def test_newer_revision_is_assumed_to_run_the_running_firmware(hub, serial_device, capsys):
    hub.device_info = device_info(hub_version=(2, 4, 0), finger_versions=((2, 4, 0), (2, 4, 0)))

    bh.detect_hardware_revision(serial_device, False)

    assert "hardware revision 2.4, newer than this script knows: assuming it runs 2.x firmware like 2.3" in \
        capsys.readouterr().err


def test_finger_rows_with_a_bad_checksum_are_ignored(hub, serial_device):
    hub.device_info = (device_info_row(0x03, (0, 0, 0), b"TH100031")
                       + device_info_row(0x02, (2, 2, 0), b"TP222015", valid_checksum=False)
                       + device_info_row(0x02, (2, 3, 0), b"TP222016"))

    assert bh.detect_hardware_revision(serial_device, False)[0] == "2.3"
