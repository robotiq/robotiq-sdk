"""Display of the device info of the Master Hub and its fingers (USB command 0xE4, see protocol.py)."""

from typing import List, Optional, Tuple

from protocol import DeviceInfo

MODELS = {0x01: "TSF-85", 0x02: "TSF-140", 0x03: "Hub"}
# The finger labels read TSF85-23130 where the EEPROM holds TP123130
LABEL_PREFIXES = {"TP1": "TSF85-", "TP2": "TSF140-"}
DEVICE_NAMES = ["Hub", "Finger 0", "Finger 1"]

UNSUPPORTED_LINES = ["Hardware revision and serial numbers: not available before firmware 2.1.0"]


def format_version(version: Tuple[int, int, int]) -> str:
    return ".".join(str(x) for x in version)


def label(info: DeviceInfo) -> str:
    for prefix, replacement in LABEL_PREFIXES.items():
        if info.serial_number.startswith(prefix):
            return replacement + info.serial_number[len(prefix):]
    return info.serial_number


def device_info_lines(rows: Optional[List[Optional[DeviceInfo]]]) -> List[str]:
    if rows is None:
        return ["Hardware revision and serial numbers: no valid reply from the hub"]

    lines = []
    for name, info in zip(DEVICE_NAMES, rows):
        if info is None:
            lines.append(f"{name} not available")
            continue
        revision = format_version(info.hardware_version) if info.hardware_version else "not programmed"
        model = MODELS.get(info.product_code, f"0x{info.product_code:02X}")
        year = info.production_year or "?"
        lines.append(f"{name:<9}{model:<8}hardware revision {revision:<16}serial {label(info):<13}year {year}")
    return lines
