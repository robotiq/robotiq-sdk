# Bootloader Host

Flashes firmware to the Master Hub via its USB bootloader.

## Requirements

```
pip install hidapi pyserial
```

Run the commands below from `tsf/firmware/`.

On Linux, run the script with `sudo`. If you installed the packages in a
virtual environment, give its interpreter to `sudo`, for example
`sudo .venv/bin/python bootloader_host.py`. The `hidapi` wheel talks to the
bootloader through libusb, and its USB device (`04b4:b71d`) is root-only on a
stock Ubuntu: run as a regular user, the script reboots the hub into its
bootloader, can't open it, and leaves the hub there. A udev rule giving your
user access to `04b4:b71d` works too.

## Firmware compatibility

Each hardware revision runs a firmware family. A hub and its fingers always
share the same hardware revision.

| Hardware revision | Firmware | Latest |
|---|---|---|
| 2.1 | 1.x | 1.1.8 |
| 2.2 | 2.x | not in `bin/` yet |
| 2.3 | 2.x | not in `bin/` yet |

No 2.x firmware is released in `bin/` yet: on a 2.2 or 2.3 hub the script
stops before rebooting it, and nothing is flashed.

The script reads the hardware revision from the EEPROM, where production
writes it since June 2026. On older units, the two digits after the dash of
a finger label give it: `TSF140-21xxx` is 2.1, `TSF140-22xxx` is 2.2,
`TSF85-23xxx` is 2.3. Finger serial numbers are now assigned sequentially,
so this doesn't hold for newer units: ask Robotiq support, with the hub
serial number.

Don't flash firmware from the other family: the hub also reprograms its
fingers with the firmware it carries.

## Usage

Flash the latest firmware for the connected hub. The hardware revision is
detected automatically and the firmware is taken from `bin/`:

```
python bootloader_host.py
```

If the hub runs firmware newer than the latest in `bin/` (a development
build, for example), this is refused rather than downgrading it.

Flash a specific firmware file, older than the running one if needed. It is
refused if it doesn't match the hardware revision:

```
python bootloader_host.py --firmware path/to/Master_hub_X.Y.Z.cyacd
```

`--latest` is still accepted. It is now the default behaviour.

### `--force`

Skips the hardware compatibility check, after asking you to type `FORCE`.
It doesn't skip the downgrade refusal: name the older file with `--firmware`
instead. With `--firmware`, it flashes a hub whose hardware revision can't
be detected:

- a hub already in its bootloader, for example after an interrupted update;
- a unit made before June 2026, whose EEPROM holds no hardware revision.

Find the hardware revision (see above), then pass the firmware made for it:

```
python bootloader_host.py --firmware bin/Master_hub_X.Y.Z.cyacd --force
```

When an update fails with the hub left in its bootloader, the script prints
this command with the file it was flashing.

> **Warning:** flashing firmware made for another hardware revision can leave
> the hub and its fingers unusable. Only use `--force` if Robotiq support told
> you to.

## How it works

1. Searches for the Master Hub on serial (tries new VID/PID, then falls back to old)
2. Detects the hardware revision while the firmware is running:
   - asks the firmware for its version: `1.x` means 2.1;
   - on `2.x`, reads the device info stored in EEPROM at production. The
     hardware revision comes from the hub, or from the fingers when the hub
     has none. A revision newer than the script knows is assumed to run the
     firmware the hub already runs, with a warning. No revision, fingers
     that disagree, unexpected device info, or an older revision the script
     doesn't know leave it unknown.
3. Picks the firmware and checks that it matches the hardware revision. By
   default it doesn't downgrade a hub that runs firmware newer than `bin/`
4. Reads the `.cyacd` file and checks that it is for this chip, so a missing
   or corrupt file is caught before the hub reboots
5. Sends a reboot-to-bootloader command over serial
6. Waits up to 30 seconds for the HID bootloader to appear (resets of 2 s
   to 22 s were measured on hubs running 2.2.0)
7. Checks the chip again against the bootloader, flashes the firmware,
   verifies the application checksum (when the bootloader was built with
   that command) and exits the bootloader

## Tests

`tests/` runs the script against a simulated hub: no hardware, `hidapi` or
`pyserial` needed. It covers firmware selection, `.cyacd` parsing, the reply
parser, the hardware revision detection, every refusal of the command line,
the bootloader wait and flashing. The `firmware` job of
`.github/workflows/tsf.yml` runs it on every pull request.

```
pip install pytest
python -m pytest tests
```

It can't catch what only real hardware shows (reset times, USB permissions):
test on a hub of each hardware revision before releasing.

## License

`bootloader_host.py` is a derivative of
[cyrozap/Cypress-HID-Bootloader-Host](https://github.com/cyrozap/Cypress-HID-Bootloader-Host)
(© 2016 Forest Crossman) and is licensed under the **GNU General Public License
v3.0** — see [LICENSE](LICENSE). This is the one GPL-licensed component in an
otherwise BSD-3-Clause repository; it is a standalone tool, not linked by the SDK.
