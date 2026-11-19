import pytest

from device_info import device_info_lines
from fake_serial import TH100032_DEVICE_INFO, FakeSerial, reply
from protocol import DeviceInfo, parse_devices_info, read_devices_info, supports_devices_info


def row(product_code, version, serial, year=2026, valid_checksum=True):
    data = bytearray([0x01, product_code, *version]) + serial + year.to_bytes(2, "big")
    data.append((sum(data) + (0 if valid_checksum else 1)) & 0xFF)
    return bytes(data)


def test_parses_the_rows_of_a_real_hub():
    hub, finger_0, finger_1 = parse_devices_info(TH100032_DEVICE_INFO)

    assert hub == DeviceInfo(product_code=0x03, hardware_version=None, serial_number="TH100032",
                             production_year=2026)
    assert finger_0 == DeviceInfo(product_code=0x01, hardware_version=(2, 3, 1), serial_number="TP123130",
                                  production_year=2026)
    assert finger_1.serial_number == "TP123053"


def test_a_row_with_a_bad_checksum_is_not_available():
    data = row(0x03, (2, 2, 0), b"TH100031", valid_checksum=False) + TH100032_DEVICE_INFO[16:]

    assert parse_devices_info(data)[0] is None


def test_a_blank_row_is_not_available():
    data = TH100032_DEVICE_INFO[:32] + bytes(16)

    assert parse_devices_info(data)[2] is None


def test_a_reply_of_the_wrong_size_is_not_available():
    assert parse_devices_info(TH100032_DEVICE_INFO[:32]) is None


def test_reads_the_reply_to_0xe4():
    port = FakeSerial(answers={0xE4: reply(0xE4, TH100032_DEVICE_INFO)})

    rows = read_devices_info(port, timeout=0.05)

    assert bytes(port.written) == bytes([0x9A, 0x00, 0xE4, 0x00])
    assert rows == parse_devices_info(TH100032_DEVICE_INFO)


def test_skips_a_cut_stream_packet_whose_tail_looks_like_a_header():
    noise = bytes([0x12, 0x9A, 0x00, 0x58, 0x40]) + reply(0x58, b"\x01\x02")
    port = FakeSerial(answers={0xE4: reply(0xE4, TH100032_DEVICE_INFO)}, noise=noise)

    assert read_devices_info(port, timeout=0.05) == parse_devices_info(TH100032_DEVICE_INFO)


def test_skips_a_header_of_the_right_length_for_another_command():
    noise = bytes([0x12, 0x9A, 0x00, 0x58, 0x30])
    port = FakeSerial(answers={0xE4: reply(0xE4, TH100032_DEVICE_INFO)}, noise=noise)

    assert read_devices_info(port, timeout=0.05) == parse_devices_info(TH100032_DEVICE_INFO)


def test_waits_for_a_reply_split_over_several_reads():
    port = FakeSerial(answers={0xE4: reply(0xE4, TH100032_DEVICE_INFO)}, chunk=7)

    assert read_devices_info(port, timeout=0.05) == parse_devices_info(TH100032_DEVICE_INFO)


def test_skips_stream_data_that_looks_like_a_0xe4_header_of_the_wrong_length():
    noise = reply(0x58, bytes([0x9A, 0x00, 0xE4, 0x02, 0x11, 0x22]))
    port = FakeSerial(answers={0xE4: reply(0xE4, TH100032_DEVICE_INFO)}, noise=noise)

    assert read_devices_info(port, timeout=0.05) == parse_devices_info(TH100032_DEVICE_INFO)


def test_serial_number_padding_is_stripped():
    rows = parse_devices_info(row(0x03, (2, 2, 0), b"TH10003\xff") + TH100032_DEVICE_INFO[16:])

    assert rows[0].serial_number == "TH10003"


def test_non_ascii_serial_bytes_are_replaced():
    rows = parse_devices_info(row(0x03, (2, 2, 0), b"TH1000\xc3\x28") + TH100032_DEVICE_INFO[16:])

    assert rows[0].serial_number == "TH1000\ufffd("


@pytest.mark.parametrize("version, supported", [
    ("1.1.8", False), ("2.0.0", False), ("2.1.0", True), ("2.2.0\x00", True), ("", True), ("garbage", True)])
def test_supports_devices_info_from_firmware_2_1_0(version, supported):
    assert supports_devices_info(version) is supported


def test_firmware_1_x_does_not_answer():
    assert read_devices_info(FakeSerial(), timeout=0.05) is None


def test_a_hub_without_version_says_so_even_when_its_fingers_have_one():
    assert device_info_lines(parse_devices_info(TH100032_DEVICE_INFO)) == [
        "Hub      Hub     hardware revision not programmed  serial TH100032     year 2026",
        "Finger 0 TSF-85  hardware revision 2.3.1           serial TSF85-23130  year 2026",
        "Finger 1 TSF-85  hardware revision 2.3.1           serial TSF85-23053  year 2026",
    ]


def test_lines_of_a_hub_with_its_own_version():
    rows = parse_devices_info(row(0x03, (2, 2, 0), b"TH100031")
                              + row(0x02, (2, 2, 0), b"TP222015")
                              + row(0x02, (2, 2, 0), b"TP222016", year=2025))

    assert device_info_lines(rows) == [
        "Hub      Hub     hardware revision 2.2.0           serial TH100031     year 2026",
        "Finger 0 TSF-140 hardware revision 2.2.0           serial TSF140-22015 year 2026",
        "Finger 1 TSF-140 hardware revision 2.2.0           serial TSF140-22016 year 2025",
    ]


@pytest.mark.parametrize("blank", [(0x00, 0x00, 0x00), (0xFF, 0xFF, 0xFF)])
def test_lines_of_units_without_a_programmed_revision(blank):
    rows = parse_devices_info(row(0x03, blank, b"TH100010")
                              + row(0x01, blank, b"TP121001")
                              + row(0x01, blank, b"TP121002"))

    assert [line.split(" serial")[0].rstrip() for line in device_info_lines(rows)] == [
        "Hub      Hub     hardware revision not programmed",
        "Finger 0 TSF-85  hardware revision not programmed",
        "Finger 1 TSF-85  hardware revision not programmed",
    ]


@pytest.mark.parametrize("year", [0, 0xFFFF])
def test_a_year_never_written_is_unknown(year):
    rows = parse_devices_info(row(0x03, (2, 2, 0), b"TH100031", year=year) + TH100032_DEVICE_INFO[16:])

    assert device_info_lines(rows)[0].endswith("year ?")


def test_an_unknown_product_code_is_shown_in_hex():
    rows = parse_devices_info(row(0x07, (2, 2, 0), b"TX100031") + TH100032_DEVICE_INFO[16:])

    assert device_info_lines(rows)[0].startswith("Hub      0x07    hardware revision 2.2.0")


@pytest.mark.parametrize("hub_row", [
    row(0x02, (0, 0, 0), b"TP222016", year=0),
    row(0x07, (255, 255, 255), b"TP222016", year=0xFFFF),
    row(0x02, (200, 200, 200), b"TP222016", year=9999),
])
def test_lines_fit_the_80_columns_of_the_monitor(hub_row):
    assert all(len(line) <= 80 for line in device_info_lines(parse_devices_info(hub_row + TH100032_DEVICE_INFO[16:])))


def test_lines_of_a_missing_finger():
    lines = device_info_lines(parse_devices_info(TH100032_DEVICE_INFO[:32] + bytes(16)))

    assert lines[2] == "Finger 1 not available"


def test_lines_without_a_valid_reply():
    assert device_info_lines(None) == ["Hardware revision and serial numbers: no valid reply from the hub"]
