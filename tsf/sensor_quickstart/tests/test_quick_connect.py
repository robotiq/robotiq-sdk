import io

import pytest

pytest.importorskip("serial")

from fake_serial import TH100032_DEVICE_INFO, FakeSerial, reply
from protocol import SensorData
from quick_connect import SensorMonitor


def connected_monitor(firmware_version, port):
    monitor = SensorMonitor()
    monitor.serial_port = port
    monitor.firmware_version = firmware_version
    return monitor


def test_prints_the_device_info_at_connection(capsys):
    monitor = connected_monitor("2.2.0", FakeSerial(answers={0xE4: reply(0xE4, TH100032_DEVICE_INFO)}))

    monitor.read_devices_info()

    out = capsys.readouterr().out
    assert "Hub      Hub     hardware revision not programmed  serial TH100032     year 2026" in out
    assert "Finger 1 TSF-85  hardware revision 2.3.1           serial TSF85-23053  year 2026" in out


def test_connect_reads_the_firmware_version_then_the_device_info(monkeypatch, capsys):
    port = FakeSerial(answers={0xE3: reply(0xE3, b"2.3.1\x00"), 0xE4: reply(0xE4, TH100032_DEVICE_INFO)})
    monkeypatch.setattr("quick_connect.serial.Serial", lambda **settings: port)
    monkeypatch.setattr("quick_connect.time.sleep", lambda seconds: None)
    monitor = SensorMonitor()

    assert monitor.connect("/dev/ttyFAKE")

    assert port.written == bytes([0x9A, 0x00, 0xE3, 0x00, 0x9A, 0x00, 0xE4, 0x00])
    assert "Finger 0 TSF-85  hardware revision 2.3.1" in capsys.readouterr().out


@pytest.mark.parametrize("version", ["1.1.8", "2.0.0"])
def test_does_not_ask_firmware_before_2_1_0_for_the_device_info(version, capsys):
    port = FakeSerial()
    monitor = connected_monitor(version, port)

    monitor.read_devices_info()

    assert port.written == b""
    assert "Hardware revision and serial numbers: not available before firmware 2.1.0" in capsys.readouterr().out


def test_says_when_a_recent_hub_does_not_answer(capsys):
    monitor = connected_monitor("2.2.0", FakeSerial())

    monitor.read_devices_info()

    assert "Hardware revision and serial numbers: no valid reply from the hub" in capsys.readouterr().out


def test_the_display_shows_the_device_info(monkeypatch):
    monitor = connected_monitor("2.2.0", FakeSerial(answers={0xE4: reply(0xE4, TH100032_DEVICE_INFO)}))
    monitor.read_devices_info()
    screen = io.StringIO()
    monkeypatch.setattr("sys.stdout", screen)

    monitor.display_sensor_data(SensorData())

    assert "Finger 0 TSF-85  hardware revision 2.3.1           serial TSF85-23130  year 2026" in screen.getvalue()
