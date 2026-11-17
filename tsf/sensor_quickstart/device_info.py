"""
Device info of the Master Hub and its fingers (USB command 0xE4, firmware 2.1.0 and later):
model, hardware revision, serial number and production year, written in EEPROM at production.
"""

import re
import time
from dataclasses import dataclass
from typing import List, Optional, Tuple

from protocol import USB_PACKET_START_BYTE

USB_COMMAND_GET_DEVICES_INFO = 0xE4
FIRST_FIRMWARE_WITH_DEVICES_INFO = (2, 1, 0)

# EEA_row_struct: id, hardware_version[4], serial_number[8], production_year[2] (MSB first), checksum.
# hardware_version is [product code, major, minor, patch].
ROW_SIZE = 16
ROW_COUNT = 3  # Hub, finger 0, finger 1
ROW_ID = 0x01

MODELS = {0x01: "TSF-85", 0x02: "TSF-140", 0x03: "Hub"}
# The finger labels read TSF85-23130 where the EEPROM holds TP123130
LABEL_PREFIXES = {"TP1": "TSF85-", "TP2": "TSF140-"}
DEVICE_NAMES = ["Hub", "Finger 0", "Finger 1"]


@dataclass(frozen=True)
class DeviceInfo:
    product_code: int
    hardware_version: Optional[Tuple[int, int, int]]  # None when production didn't write it
    serial_number: str
    production_year: Optional[int]


def parse_row(row: bytes) -> Optional[DeviceInfo]:
    """Returns the device info of a row, or None if the row is invalid (bad checksum, no device)."""
    if (sum(row[:-1]) & 0xFF) != row[-1] or row[0] != ROW_ID:
        return None
    version = tuple(row[2:5])
    year = (row[13] << 8) | row[14]
    return DeviceInfo(
        product_code=row[1],
        # Production wrote only the product code until June 2026
        hardware_version=version if any(version) else None,
        serial_number=row[5:13].strip(b"\x00\xff ").decode("ascii", errors="replace"),
        production_year=year if year not in (0, 0xFFFF) else None,
    )


def parse_devices_info(data: bytes) -> Optional[List[Optional[DeviceInfo]]]:
    """Returns the hub then each finger, None for an invalid row, or None if the reply has the wrong size."""
    if len(data) != ROW_SIZE * ROW_COUNT:
        return None
    return [parse_row(data[i:i + ROW_SIZE]) for i in range(0, len(data), ROW_SIZE)]


def request_reply(serial_port, command: int, length: int, timeout: float = 1.0) -> Optional[bytes]:
    """Sends a command and returns the data of its reply of the given length, or None on timeout."""
    serial_port.reset_input_buffer()
    serial_port.write(bytes([USB_PACKET_START_BYTE, 0x00, command, 0x00]))
    serial_port.flush()

    buffer = bytearray()
    deadline = time.time() + timeout
    while time.time() < deadline:
        buffer += serial_port.read(serial_port.in_waiting or 1)
        while True:
            start = buffer.find(USB_PACKET_START_BYTE)
            if start < 0:
                buffer.clear()
                break
            del buffer[:start]
            # A hub left streaming by an earlier run, or a stream packet cut by the flush, can hold 0x9A ?? command
            # in its data: only accept a header carrying both the command and the length expected.
            if (len(buffer) >= 3 and buffer[2] != command) or (len(buffer) >= 4 and buffer[3] != length):
                del buffer[:1]
                continue
            if len(buffer) < 4 + length:
                break
            return bytes(buffer[4:4 + length])
    return None


def supports_devices_info(firmware_version: str) -> bool:
    """False for firmware older than 2.1.0, which ignores 0xE4. True when the version is unknown: try anyway."""
    match = re.search(r"(\d+)\.(\d+)\.(\d+)", firmware_version)
    return not match or tuple(int(x) for x in match.groups()) >= FIRST_FIRMWARE_WITH_DEVICES_INFO


def read_devices_info(serial_port, timeout: float = 1.0) -> Optional[List[Optional[DeviceInfo]]]:
    """Reads the device info of the hub and its fingers, or None without a valid reply."""
    data = request_reply(serial_port, USB_COMMAND_GET_DEVICES_INFO, ROW_SIZE * ROW_COUNT, timeout)
    return parse_devices_info(data) if data is not None else None


def format_version(version: Tuple[int, int, int]) -> str:
    return ".".join(str(x) for x in version)


def label(info: DeviceInfo) -> str:
    for prefix, replacement in LABEL_PREFIXES.items():
        if info.serial_number.startswith(prefix):
            return replacement + info.serial_number[len(prefix):]
    return info.serial_number


UNSUPPORTED_LINES = ["Hardware revision and serial numbers: not available before firmware 2.1.0"]


def device_info_lines(rows: Optional[List[Optional[DeviceInfo]]]) -> List[str]:
    """One line per device: model, hardware revision, serial number and production year."""
    if rows is None:
        return ["Hardware revision and serial numbers: no valid reply from the hub"]

    lines = []
    for name, info in zip(DEVICE_NAMES, rows):
        if info is None:
            lines.append(f"{name} not available")
            continue
        revision = format_version(info.hardware_version) if info.hardware_version else "not programmed"
        model = MODELS.get(info.product_code, f"0x{info.product_code:02X}")
        year = info.production_year or "unknown"
        # 80 columns at most, the width of the monitor
        lines.append(f"{name:<9}{model:<8}hardware revision {revision:<16}serial {label(info):<13}year {year}")
    return lines
