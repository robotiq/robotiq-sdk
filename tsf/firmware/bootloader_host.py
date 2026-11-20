#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2016  Forest Crossman <cyrozap@gmail.com>
# Copyright (C) 2026  Robotiq
#
# Based on cyrozap/Cypress-HID-Bootloader-Host; modified 2026 by Robotiq for
# Master Hub integration (device VID/PIDs, firmware selection, the serial
# reboot-to-bootloader handshake, the hardware revision compatibility check,
# and the application checksum verification after programming).
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# "04b4:f13b" "../Master_hub.cydsn/CortexM3/ARM_GCC_541/Release/Master_hub.cyacd"

import argparse
import binascii
import sys

import hid  # requires the 'hidapi' package (pip install hidapi), works on Windows, Linux, and macOS

import serial
import serial.tools.list_ports

import time
import os
import glob
import re

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BIN_DIR = os.path.join(SCRIPT_DIR, "bin")

BOOTLOADER_VID = 0x04B4
BOOTLOADER_PID = 0xB71D

MASTER_HUB_APP_VID_OLD = 0x04B4
MASTER_HUB_APP_PID_OLD = 0xF232

MASTER_HUB_APP_VID = 0x16D0
MASTER_HUB_APP_PID = 0x14CC

# Master Hub application USB protocol (see Master_hub.cydsn/usb_comm.h)
USB_PACKET_START_BYTE = 0x9A
USB_COMMAND_AUTOSEND_ASYNC_SENSORS = 0x59
USB_COMMAND_ENTER_BOOTLOADER = 0xE2
USB_COMMAND_GET_FIRMWARE_VERSION = 0xE3  # Answered by every firmware released
USB_COMMAND_GET_DEVICES_INFO = 0xE4      # Firmware 2.1.0 and later only

# Device info rows (EEA_row_struct): id, hardware_version[4], serial_number[8], production_year[2], checksum.
# hardware_version is [product code, major, minor, patch]; production wrote zeros after the code until June 2026.
DEVICE_INFO_ROW_SIZE = 16
DEVICE_INFO_ROW_COUNT = 3  # Hub, finger 1, finger 2
DEVICE_INFO_ID = 0x01
DEVICE_INFO_HUB_TYPE = 0x03
DEVICE_INFO_HUB_SERIAL_PREFIX = b"TH1"

# The hub and its fingers always share the same hardware revision, and each revision runs a firmware
# family (major version). HW_REV_HINT tells users how to find the revision.
FIRMWARE_1X = 1
FIRMWARE_2X = 2
FIRMWARE_FAMILY = {
    "2.1": FIRMWARE_1X,
    "2.2": FIRMWARE_2X,
    "2.3": FIRMWARE_2X,
}
# Finger serial numbers are now assigned sequentially: only the older ones give the revision
HW_REV_HINT = ("On older units, the two digits after the dash of a finger label give the hardware revision: "
               "TSF140-21xxx is 2.1, TSF140-22xxx is 2.2, TSF85-23xxx is 2.3. Otherwise ask Robotiq support, "
               "with the hub serial number.")

# The hub takes a while to reset after USB_COMMAND_ENTER_BOOTLOADER: 2 s to 22 s measured on hubs running 2.2.0
BOOTLOADER_WAIT_TIMEOUT = 30


def parse_version(version_str, parts=3):
    """Returns a version string like "2.2.0" (or "2.2" with parts=2) as a tuple of ints, or None if it isn't one."""
    match = re.match(r"^\d+(\.\d+){%d}$" % (parts - 1), version_str.strip())
    return tuple(int(x) for x in match.group(0).split(".")) if match else None


def parse_revision(hw_rev):
    return parse_version(hw_rev, parts=2)


def family_of_revision(hw_rev):
    """Returns the firmware family of a hardware revision, or None if this script doesn't know it.

    A revision newer than the newest known one of the same hardware major is assumed to run the same firmware.
    """
    if hw_rev in FIRMWARE_FAMILY:
        return FIRMWARE_FAMILY[hw_rev]
    revision = parse_revision(hw_rev)
    same_major = [rev for rev in FIRMWARE_FAMILY if parse_revision(rev)[0] == revision[0]]
    if same_major and revision > parse_revision(max(same_major, key=parse_revision)):
        return FIRMWARE_FAMILY[max(same_major, key=parse_revision)]
    return None


def firmware_version_from_path(path):
    """Returns the version of a Master_hub_X.Y.Z.cyacd file from its name, or None."""
    match = re.match(r"^Master_hub_(.+)\.cyacd$", os.path.basename(path))
    return parse_version(match.group(1)) if match else None


def find_latest_firmware(family):
    """Find the latest Master_hub firmware file of a family (major version) in the bin/ directory."""
    files = {}
    for path in glob.glob(os.path.join(BIN_DIR, "Master_hub_*.cyacd")):
        version = firmware_version_from_path(path)
        if version and version[0] == family:
            files[version] = path
    return files[max(files)] if files else None


def is_downgrade(running_version, firmware_version):
    """Across families the versions don't compare: that is the hardware revision check's job."""
    return bool(running_version and firmware_version and running_version[0] == firmware_version[0]
                and running_version > firmware_version)


def format_version(version):
    return ".".join(str(x) for x in version) if version else "unknown"


JTAG_ID = 0x2E15A069  # CY8C5288LTI-LP090
DEVICE_REV = 0
BOOTLOADER_REV = 0


class Bootloader():
    MAX_DATA_LENGTH = 64-7-9

    STATUSES = {
        0x00: "CYRET_SUCCESS",
        0x03: "BOOTLOADER_ERR_LENGTH",
        0x04: "BOOTLOADER_ERR_DATA",
        0x05: "BOOTLOADER_ERR_CMD",
        0x08: "BOOTLOADER_ERR_CHECKSUM",
        0x09: "BOOTLOADER_ERR_ARRAY",
        0x0a: "BOOTLOADER_ERR_ROW",
        0x0c: "BOOTLOADER_ERR_APP",
        0x0d: "BOOTLOADER_ERR_ACTIVE",
        0x0e: "BOOTLOADER_ERR_CALLBACK",
        0x0f: "BOOTLOADER_ERR_UNK"
    }

    def __init__(self, vid=0, pid=0, serial=None):
        self._device = hid.device()
        try:
            self._device.open(vid, pid)
        except OSError as error:
            sys.stderr.write("Error: {}\n".format(error))
            sys.stderr.write("Make sure you have permissions to access the HID device (try running with sudo or adding a udev rule).\n")
            raise

        self.jtag_id = JTAG_ID
        self.device_revision = DEVICE_REV
        self.bootloader_revision = BOOTLOADER_REV

    def _checksum(self, data):
        '''16-bit packet checksum.

        :param data: The data to sum
        :type data: bytes or list of 8-bit integers
        :returns: checksum
        :rtype: int
        '''
        checksum = 0
        for byte in data:
            checksum += byte
        checksum = (~checksum + 1) & 0xffff
        return checksum

    def _make_packet(self, command, data=[]):
        '''Generates a packet to send the bootloader.

        :param command: The command to send
        :type command: 8-bit int
        :param data: The data to send
        :type data: bytes or list of 8-bit integers
        :returns: Packet data
        :rtype: list of 8-bit integers
        '''
        packet = []
        packet.append(0x00) # Byte supplémentaire à envoyé pour Report ID du protocol USB. (Ce Byte n'est pas en lien avec le bootloader)
        packet.append(0x01)

        packet.append(command)

        data_length = len(data)
        assert data_length <= (64 - 7)
        packet.append(data_length & 0xff)
        packet.append((data_length >> 8) & 0xff)

        for byte in data:
            packet.append(byte & 0xff)

        checksum = self._checksum(packet)
        packet.append(checksum & 0xff)
        packet.append((checksum >> 8) & 0xff)

        packet.append(0x17)
        return packet

    def _parse_response(self, response_data):
        '''Parses the bootloader's response.

        :param response_data: The response_data to parse
        :type response_data: list of 8-bit integers
        :returns: Response data
        :rtype: dict
        '''
        response = {
            "status": "BOOTLOADER_ERR_UNK",
            "data": [],
            "checksum_ok": False,
        }

        sop = response_data[0]
        assert sop == 0x01

        response["status"] = self.STATUSES[response_data[1]]

        data_length = (response_data[3] << 8) | response_data[2]

        if data_length > 0:
            response["data"] = response_data[4:4+data_length]

        checksum_received = response_data[4+data_length]
        checksum_received |= response_data[4+data_length+1] << 8
        checksum_calculated = self._checksum(response_data[:4+data_length])
        response["checksum_ok"] = (checksum_calculated == checksum_received)

        eop = response_data[4+data_length+2]
        assert eop == 0x17

        return response

    @staticmethod
    def _answered(response, status):
        return response["checksum_ok"] and response["status"] == status

    def send_command(self, command, data=[]):
        '''Sends a command with data to the bootloader.

        :param command: The command to send
        :type command: 8-bit int
        :param data: The data to send
        :type data: bytes or list of 8-bit integers
        :returns: Response data
        :rtype: dict
        '''
        packet = self._make_packet(command, data)
        self._device.write(bytes(packet))

        response_data = self._device.read(64)
        response = self._parse_response(response_data)
        return response

    def enter_bootloader(self):
        '''Sends the "Enter Bootloader" command.

        :returns: Success/failure
        :rtype: bool
        '''
        #return True
        response = self.send_command(0x38)
        if self._answered(response, "CYRET_SUCCESS"):
            self.jtag_id = response["data"][0]
            self.jtag_id |= response["data"][1] << 8
            self.jtag_id |= response["data"][2] << 16
            self.jtag_id |= response["data"][3] << 24
            self.device_revision = response["data"][4]
            self.bootloader_revision = response["data"][5:8][::-1]
            return True
        else:
            return False

    def program_row(self, array_id, row_number, data):
        '''Programs a row of flash.

        :param array_id: The array_id of the row to program
        :type array_id: 8-bit int
        :param row_number: The row_number of the row to program
        :type row_number: 16-bit int
        :param data: The data to program
        :type data: bytes or list of 8-bit integers
        :returns: Success/failure
        :rtype: bool
        '''
        arguments = [array_id, (row_number & 0xff), (row_number >> 8)]
        response = self.send_command(0x39, arguments + data)
        if self._answered(response, "CYRET_SUCCESS"):
            return True
        else:
            return False

    def erase_row(self, array_id, row_number):
        '''Erases a row of flash.

        :param array_id: The array_id of the row to erase
        :type array_id: 8-bit int
        :param row_number: The row_number of the row to erase
        :type row_number: 16-bit int
        :returns: Success/failure
        :rtype: bool
        '''
        arguments = [array_id, (row_number & 0xff), (row_number >> 8)]
        response = self.send_command(0x34, arguments)
        if self._answered(response, "CYRET_SUCCESS"):
            return True
        else:
            return False

    def send_data(self, data):
        '''Sends data to the bootloader to be used by another command.

        :param data: The data to send
        :type data: bytes or list of 8-bit integers
        :returns: Success/failure
        :rtype: bool
        '''
        response = self.send_command(0x37, data)
        if self._answered(response, "CYRET_SUCCESS"):
            return True
        else:
            return False

    def verify_checksum(self):
        '''Sends the "Verify Checksum" command.

        :returns: True if the application checksum is valid, None if this bootloader wasn't built with the command
        :rtype: bool or None
        '''
        response = self.send_command(0x31)
        if self._answered(response, "BOOTLOADER_ERR_CMD"):
            return None
        if self._answered(response, "CYRET_SUCCESS") and len(response["data"]) > 0:
            return response["data"][0] != 0
        else:
            return False

    def exit_bootloader(self):
        '''Sends the "Exit Bootloader" command.'''
        packet = self._make_packet(0x3b, [])
        self._device.write(bytes(packet))

    def flash(self, firmware):
        ret = False
        firmware_data = firmware.firmware
        for row in firmware_data:
            array_id = row[0]
            row_number = row[1]
            data = list(row[2])
            if len(data) >= self.MAX_DATA_LENGTH:
                full_packets = len(data)//self.MAX_DATA_LENGTH
                partial_packet_length = len(data)%self.MAX_DATA_LENGTH
                if partial_packet_length > 0:
                    for i in range(0, full_packets):
                        ret = self.send_data(data[self.MAX_DATA_LENGTH*i:self.MAX_DATA_LENGTH*i+self.MAX_DATA_LENGTH])
                        if not ret:
                            return False
                    ret = self.program_row(array_id, row_number, data[self.MAX_DATA_LENGTH*full_packets:self.MAX_DATA_LENGTH*full_packets+partial_packet_length])
                    if not ret:
                        return False
                else:
                    for i in range(0, full_packets-1):
                        ret = self.send_data(data[self.MAX_DATA_LENGTH*i:self.MAX_DATA_LENGTH*i+self.MAX_DATA_LENGTH])
                        if not ret:
                            return False
                    ret = self.program_row(array_id, row_number, data[self.MAX_DATA_LENGTH*(full_packets-1):self.MAX_DATA_LENGTH*(full_packets-1)+self.MAX_DATA_LENGTH])
                    if not ret:
                        return False
            else:
                ret = self.program_row(array_id, row_number, data)
                if not ret:
                    return False
        
        return ret

class Cyacd():
    def __init__(self, firmware_file):
        self.file = firmware_file
        self.silicon_id = None
        self.silicon_revision = None
        self.checksum_type = None
        self.firmware = None

    def _checksum(self, data):
        '''Single-byte flash row checksum.

        :param data: The data to sum
        :type data: bytes or list of 8-bit integers
        :returns: checksum
        :rtype: int
        '''
        checksum = 0
        for byte in data:
            checksum += byte
        checksum = (~checksum + 1) & 0xff
        return checksum

    def parse(self):
        '''Read's the firmware into a list of (array_id, row_number, data)
        tuples.
        '''
        lines = self.file.readlines()

        header_line = lines.pop(0).rstrip('\r\n')
        header = binascii.a2b_hex(header_line)
        self.silicon_id = header[3]
        self.silicon_id |= header[2] << 8
        self.silicon_id |= header[1] << 16
        self.silicon_id |= header[0] << 24
        self.silicon_revision = header[4]
        self.checksum_type = header[5]

        firmware = []
        for line in lines:
            line = binascii.a2b_hex(line[1:].rstrip('\r\n'))
            array_id = line[0]
            row_number = (line[1] << 8) | line[2]
            data_length = (line[3] << 8) | line[4]
            data = line[5:5+data_length]
            checksum = line[5+data_length]
            checksum_calculated = self._checksum(line[:-1])
            assert checksum == checksum_calculated
            firmware.append((array_id, row_number, data))

        self.firmware = firmware


def open_serial_by_vid_pid(vid, pid, baudrate=115200):
    # 1. Lister tous les ports COM disponibles
    ports = serial.tools.list_ports.comports()
    target_port = None

    for port in ports:
        # Vérification de la correspondance VID/PID
        if port.vid == vid and port.pid == pid:
            target_port = port.device
            break

    if target_port:
        try:
            ser = serial.Serial(
                port=target_port,
                baudrate=baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=1
            )
            return ser
        except Exception as e:
            sys.stderr.write("Error while opening port {}: {}\n".format(target_port, e))
            return None
    else:
        return None


def make_usb_packet(command, data=b""):
    """Builds a Master Hub application packet: start byte, CRC (unused, always 0), command, length, data."""
    return bytes([USB_PACKET_START_BYTE, 0x00, command, len(data)]) + bytes(data)


def request_usb_reply(serial_device, command, timeout=1.0):
    """Sends a command to the Master Hub application and returns the data of its reply, or None on timeout."""
    serial_device.reset_input_buffer()
    serial_device.write(make_usb_packet(command))

    buffer = bytearray()
    deadline = time.time() + timeout
    while time.time() < deadline:
        buffer += serial_device.read(serial_device.in_waiting or 1)

        # Parse every complete packet in the buffer, dropping bytes until a start byte. The flush above can cut a
        # stream packet, and a 0x9A in its tail would be taken as a header with a bogus length: only accept a header
        # carrying the command we asked for, otherwise drop one byte and rescan. The CRC byte can't help: firmware
        # 1.1.5 and later send 0, but up to 1.1.3 it is whatever was on the hub's stack.
        while True:
            start = buffer.find(USB_PACKET_START_BYTE)
            if start < 0:
                buffer.clear()
                break
            del buffer[:start]
            if len(buffer) >= 3 and buffer[2] != command:
                del buffer[:1]
                continue
            if len(buffer) < 4 or len(buffer) < 4 + buffer[3]:
                break
            return bytes(buffer[4:4 + buffer[3]])

    return None


def drain_input(serial_device, quiet=0.05, limit=0.5):
    """Reads and discards input until the port stays quiet for `quiet` seconds, for at most `limit` seconds."""
    deadline = time.time() + limit
    last_data = time.time()
    while time.time() < deadline and time.time() - last_data < quiet:
        if serial_device.in_waiting:
            serial_device.read(serial_device.in_waiting)
            last_data = time.time()
        else:
            time.sleep(0.005)


def parse_device_info_row(row):
    """Returns (hardware_version, serial_number) of a device info row, or None if it is invalid."""
    if (sum(row[:-1]) & 0xFF) != row[-1] or row[0] != DEVICE_INFO_ID:
        return None
    return row[1:5], row[5:13]


def read_hub_device_info(serial_device):
    """Reads the device info stored in EEPROM at production.

    :returns: ("valid", rows), ("blank", None) when missing, unreadable or never programmed, or
              ("inconsistent", rows) when the hub row is valid but not a hub. rows holds (hardware_version,
              serial_number) for the hub then each finger, None for an invalid finger row.
    """
    data = request_usb_reply(serial_device, USB_COMMAND_GET_DEVICES_INFO)
    if data is None or len(data) != DEVICE_INFO_ROW_SIZE * DEVICE_INFO_ROW_COUNT:
        return "blank", None

    rows = [parse_device_info_row(data[i:i + DEVICE_INFO_ROW_SIZE])
            for i in range(0, len(data), DEVICE_INFO_ROW_SIZE)]
    if rows[0] is None:
        return "blank", None

    hardware_version, serial_number = rows[0]
    if hardware_version[0] != DEVICE_INFO_HUB_TYPE or not serial_number.startswith(DEVICE_INFO_HUB_SERIAL_PREFIX):
        return "inconsistent", rows
    return "valid", rows


def revision_from_device_info(rows, family):
    """Reads the hardware revision from the hub and finger rows. The hub row wins when they differ within a family.

    :returns: the revision, or None when the EEPROM doesn't tell it or says something wrong
    """
    def revision(row):
        return "{}.{}".format(*row[0][1:3]) if row and any(row[0][1:3]) else None

    hub_revision = revision(rows[0])
    revisions = [rev for rev in [hub_revision] + [revision(row) for row in rows[1:]] if rev]
    if not revisions:
        # The firmware family alone can't rule out a cross-flash
        sys.stderr.write("Error: No hardware revision in the hub or finger rows of the EEPROM "
                         "(likely a unit made before June 2026).\n")
        return None

    families = {rev: family_of_revision(rev) for rev in revisions}
    unknown = sorted(rev for rev in families if families[rev] is None)
    if unknown:
        sys.stderr.write("Error: The EEPROM holds hardware revision {}, which this script doesn't know.\n".format(
            ", ".join(unknown)))
        return None
    if set(families.values()) != {family}:
        sys.stderr.write("Error: The EEPROM holds hardware revisions {}, which don't all run firmware {}.x.\n".format(
            ", ".join(sorted(families)), family))
        return None

    hw_rev = hub_revision or max(revisions, key=parse_revision)
    if len(families) > 1:
        sys.stderr.write("Warning: The hub and its fingers hold different hardware revisions in EEPROM ({}), "
                         "using {}.\n".format(", ".join(sorted(families)), hw_rev))
    if hw_rev not in FIRMWARE_FAMILY:
        known = max((rev for rev in FIRMWARE_FAMILY if FIRMWARE_FAMILY[rev] == family), key=parse_revision)
        sys.stderr.write("Warning: The EEPROM holds hardware revision {}, newer than this script knows: assuming it "
                         "runs {}.x firmware like {}.\n".format(hw_rev, family, known))
        hw_rev = known
    return hw_rev


def detect_hardware_revision(serial_device, old_vid_pid):
    """Detects the hardware revision of a Master Hub running its application.

    :returns: (hardware revision or None if it can't be told, running firmware version or None)
    """
    # Stop any sensor stream left running so it doesn't hide the replies. The hub doesn't ack this command, so
    # wait for the packets already in flight to land, then the port to go quiet.
    serial_device.write(make_usb_packet(USB_COMMAND_AUTOSEND_ASYNC_SENSORS, b"\x00"))
    drain_input(serial_device)

    reply = request_usb_reply(serial_device, USB_COMMAND_GET_FIRMWARE_VERSION)
    running_version = parse_version(reply.split(b"\x00")[0].decode("ascii", "replace")) if reply else None
    sys.stderr.write("Firmware running: {}\n".format(format_version(running_version)))

    # The old VID/PID predates hardware revision 2.2
    if old_vid_pid:
        return "2.1", running_version

    if running_version is None:
        sys.stderr.write("Error: The device did not report its firmware version.\n")
        return None, None

    if running_version[0] == FIRMWARE_1X:
        return "2.1", running_version

    if running_version[0] == FIRMWARE_2X:
        status, rows = read_hub_device_info(serial_device)
        if status == "blank":
            sys.stderr.write("Error: No device info in the hub EEPROM.\n")
            return None, running_version
        hardware_version, serial_number = rows[0]
        if status == "inconsistent":
            sys.stderr.write("Error: The hub EEPROM holds unexpected device info (hardware version {}, "
                             "serial number {}).\n".format(hardware_version.hex(),
                                                           serial_number.decode("ascii", "replace")))
            return None, running_version
        sys.stderr.write("Hub serial number: {}\n".format(serial_number.decode("ascii", "replace")))
        return revision_from_device_info(rows, running_version[0]), running_version

    sys.stderr.write("Error: Unknown firmware family {}.\n".format(format_version(running_version)))
    return None, running_version


def confirm_force(reasons):
    """Asks the user to confirm flashing despite failed compatibility checks."""
    sys.stderr.write("\n"
                     "WARNING: --force skips the hardware compatibility check:\n")
    for reason in reasons:
        sys.stderr.write("  - {}\n".format(reason))
    sys.stderr.write("Flashing firmware made for another hardware revision can leave the hub and its fingers\n"
                     "unusable. The hub also reprograms the fingers with the firmware it carries.\n"
                     "Only continue if Robotiq support told you to.\n"
                     "Type FORCE to continue: ")
    sys.stderr.flush()
    try:
        answer = input("")
    except EOFError:
        answer = ""
    return answer.strip() == "FORCE"


def load_firmware(path):
    """Reads and parses a .cyacd file, or exits when it can't be read or is corrupt."""
    try:
        with open(path, "r") as firmware_file:
            firmware = Cyacd(firmware_file)
            firmware.parse()
    except (OSError, ValueError, IndexError, AssertionError) as error:
        sys.stderr.write("Error: Can't read firmware file {}: {}. Nothing was flashed.\n".format(
            path, str(error) or "corrupt file"))
        sys.exit(1)
    return firmware


def needs_sudo():
    return sys.platform.startswith("linux") and os.geteuid() != 0


def rerun_hint(firmware_file, sudo=False):
    return "run again with {}--firmware {} --force.".format("sudo, with " if sudo else "", firmware_file)


def latest_firmware_per_revision(families):
    """Lists the latest firmware in bin/ for each hardware revision, e.g. "2.2 and 2.3: bin/Master_hub_2.2.0.cyacd"."""
    lines = []
    for family in sorted(families):
        firmware_file = find_latest_firmware(family)
        if firmware_file:
            revisions = " and ".join(rev for rev in sorted(FIRMWARE_FAMILY) if FIRMWARE_FAMILY[rev] == family)
            lines.append("  {}: {}".format(revisions, firmware_file))
    return "\n".join(lines)


def wait_for_bootloader(vid, pid, timeout=BOOTLOADER_WAIT_TIMEOUT, interval=0.5):
    """Polls for the bootloader HID device, returns True once it is enumerated, False after timeout seconds."""
    deadline = time.time() + timeout
    while True:
        if any(device["vendor_id"] == vid and device["product_id"] == pid for device in hid.enumerate()):
            return True
        if time.time() >= deadline:
            return False
        time.sleep(interval)


def main():
    parser = argparse.ArgumentParser(
        description="Flash firmware to the Master Hub via USB bootloader. By default, flashes the latest "
                    "firmware in bin/ for the hardware revision of the connected hub.")
    parser.add_argument("--firmware", type=str, default=None,
                        help="Path to a specific Master_hub_X.Y.Z.cyacd firmware file")
    parser.add_argument("--latest", action="store_true",
                        help="Kept for compatibility: flashing the latest firmware is now the default")
    parser.add_argument("--force", action="store_true",
                        help="Skip the hardware compatibility check (dangerous, asks for confirmation). With "
                             "--firmware, flashes a hub whose hardware revision can't be detected, for example "
                             "one already in its bootloader.")
    args = parser.parse_args()

    vid = BOOTLOADER_VID
    pid = BOOTLOADER_PID

    # Search for a device in application mode
    old_vid_pid = False
    serial_device = open_serial_by_vid_pid(MASTER_HUB_APP_VID, MASTER_HUB_APP_PID)
    if not serial_device:
        serial_device = open_serial_by_vid_pid(MASTER_HUB_APP_VID_OLD, MASTER_HUB_APP_PID_OLD)
        old_vid_pid = serial_device is not None
    app_running = bool(serial_device and serial_device.is_open)

    # Detect the hardware revision while the application is still running
    hw_rev = None
    running_version = None
    if app_running:
        sys.stderr.write("Device found on {}.\n".format(serial_device.port))
        serial_device.timeout = 0.1
        hw_rev, running_version = detect_hardware_revision(serial_device, old_vid_pid)
    else:
        sys.stderr.write("No device in application mode found. The hardware revision can't be detected.\n")

    force_reasons = []
    if hw_rev:
        sys.stderr.write("Hardware revision: {}\n".format(hw_rev))

    # Pick the firmware and check it matches the hardware revision
    if args.firmware:
        firmware_file = args.firmware
        firmware_version = firmware_version_from_path(firmware_file)
        if hw_rev is None:
            force_reasons.append("the hardware revision is unknown, so the firmware can't be checked against it")
            # The running firmware still shows a cross-family flash, the case FORCE warns about
            if running_version and firmware_version and running_version[0] != firmware_version[0]:
                force_reasons.append("firmware {} is from another family than the firmware the hub runs ({})".format(
                    format_version(firmware_version), format_version(running_version)))
        elif firmware_version is None:
            force_reasons.append("the firmware version can't be read from the file name (expected Master_hub_X.Y.Z.cyacd)")
        elif firmware_version[0] != FIRMWARE_FAMILY[hw_rev]:
            force_reasons.append("firmware {} is not made for hardware revision {} (needs {}.x)".format(
                format_version(firmware_version), hw_rev, FIRMWARE_FAMILY[hw_rev]))
    else:
        if hw_rev is None:
            sys.stderr.write("Error: Unknown hardware revision. Nothing was flashed. If you know it, pass the firmware "
                             "made for it with --firmware and --force:\n{}\n{}\n".format(
                                 latest_firmware_per_revision([running_version[0]] if running_version
                                                              else set(FIRMWARE_FAMILY.values())), HW_REV_HINT))
            sys.exit(1)
        firmware_file = find_latest_firmware(FIRMWARE_FAMILY[hw_rev])
        if not firmware_file:
            sys.stderr.write("Error: No {}.x firmware files found in {}\n".format(
                FIRMWARE_FAMILY[hw_rev], os.path.abspath(BIN_DIR)))
            sys.exit(1)
        firmware_version = firmware_version_from_path(firmware_file)
        # Hubs can run builds newer than bin/: only downgrade one when the user names the file
        if is_downgrade(running_version, firmware_version):
            sys.stderr.write("Error: The hub runs firmware {}, newer than {}, the latest in bin/. Nothing was flashed.\n"
                             "To downgrade it anyway, pass --firmware {}\n".format(
                                 format_version(running_version), format_version(firmware_version), firmware_file))
            sys.exit(1)

    sys.stderr.write("Firmware: {}\n".format(os.path.basename(firmware_file)))
    if is_downgrade(running_version, firmware_version):
        sys.stderr.write("Downgrading from {} to {}.\n".format(format_version(running_version),
                                                                format_version(firmware_version)))

    # Read the firmware and check it is for this chip before rebooting, so a bad file can't strand the hub
    firmware = load_firmware(firmware_file)
    if (firmware.silicon_id != JTAG_ID) or (firmware.silicon_revision != DEVICE_REV):
        sys.stderr.write("Error: Firmware is for silicon 0x{:08X} rev {}, the Master Hub is 0x{:08X} rev {}. "
                         "Nothing was flashed.\n".format(firmware.silicon_id, firmware.silicon_revision,
                                                         JTAG_ID, DEVICE_REV))
        sys.exit(1)

    if force_reasons:
        if not args.force:
            for reason in force_reasons:
                sys.stderr.write("Error: {}.\n".format(reason[0].upper() + reason[1:]))
            sys.stderr.write("Nothing was flashed.\n")
            sys.exit(1)
        if not confirm_force(force_reasons):
            sys.stderr.write("Aborted. Nothing was flashed.\n")
            sys.exit(1)

    # Reboot the device into bootloader
    if app_running:
        if needs_sudo():
            sys.stderr.write("Warning: Not running as root. On Linux the bootloader USB device is usually root-only: "
                             "if it can't be opened, the hub stays in its bootloader. Run with sudo or add a udev "
                             "rule (see README.md).\n")
        sys.stderr.write("Rebooting into bootloader, this can take up to 30 seconds...\n")
        serial_device.write(make_usb_packet(USB_COMMAND_ENTER_BOOTLOADER))
        serial_device.close()
    else:
        sys.stderr.write("Looking for bootloader directly...\n")

    if not wait_for_bootloader(vid, pid):
        sys.stderr.write("Bootloader not found after {} seconds!\n".format(BOOTLOADER_WAIT_TIMEOUT))
        if app_running:
            sys.stderr.write("If the hub has reached its bootloader since, {}\n".format(rerun_hint(firmware_file)))
        sys.exit(1)

    # Connect to the device's bootloader
    try:
        bootloader = Bootloader(vid, pid)
    except OSError:
        sys.stderr.write("The hub stays in its bootloader: {}\n".format(rerun_hint(firmware_file, needs_sudo())))
        sys.exit(1)
    if not bootloader.enter_bootloader():
        sys.stderr.write("Unable to communicate with bootloader! The device stays in bootloader: {}\n".format(
            rerun_hint(firmware_file)))
        sys.exit(1)

    # Make sure the firmware is being flashed to the correct chip
    if (bootloader.jtag_id != firmware.silicon_id) or (bootloader.device_revision != firmware.silicon_revision):
        sys.stderr.write("Error: Firmware is for silicon 0x{:08X} rev {}, device is 0x{:08X} rev {}. Nothing was flashed.\n".format(
            firmware.silicon_id, firmware.silicon_revision, bootloader.jtag_id, bootloader.device_revision))
        bootloader.exit_bootloader()
        sys.exit(1)

    sys.stderr.write("Programming firmware application...\n")
    if not bootloader.flash(firmware):
        sys.stderr.write("Error while programming! The device stays in bootloader: {}\n".format(rerun_hint(firmware_file)))
        sys.exit(1)

    checksum_valid = bootloader.verify_checksum()
    if checksum_valid is False:
        sys.stderr.write("Error: Application checksum is invalid! The device stays in bootloader: {}\n".format(
            rerun_hint(firmware_file)))
        sys.exit(1)

    bootloader.exit_bootloader()

    if checksum_valid is None:
        # The bootloader won't start an invalid application, but nothing here can tell whether it did
        sys.stderr.write("Programmed, but this bootloader can't verify the application checksum. If the hub doesn't "
                         "come back, {}\n".format(rerun_hint(firmware_file)))
        return
    sys.stderr.write("Device programmed successfully!\n")


if __name__ == "__main__":
    main()
