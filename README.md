# HE-4025 MeshCore SX1276 Bring-Up

This tree is a Linux/OpenWrt userspace bring-up scaffold for an HE-4025 with an
SX1276 LoRa radio on SPI.

The first milestone is deliberately small: prove that SPI can read
`RegVersion` at register `0x42`. An SX1276 should return `0x12`.

## Minimum Pins

Map these before trying to run the daemon:

| Signal | Required | Notes |
| --- | --- | --- |
| SPI MOSI | yes | HE-4025 SPI controller pin |
| SPI MISO | yes | HE-4025 SPI controller pin |
| SPI SCK | yes | HE-4025 SPI controller pin |
| NSS / CS | yes | Either SPI chip-select or a GPIO-controlled NSS |
| RESET | yes | GPIO output |
| DIO0 | yes | GPIO input, polled first |
| GND | yes | Common ground |
| 3.3V | yes | SX1276 is 3.3V logic |
| DIO1 | optional | Useful later |
| DIO2 | optional | Useful later |

## Probe SPI First

Build the tiny C probe on OpenWrt, or cross-compile it:

```sh
make sx1276-regversion
./sx1276-regversion /dev/spidev0.0
```

Expected output:

```text
SX1276 RegVersion: 0x12
```

If the value is not `0x12`, fix wiring, chip-select, SPI mode, or speed before
moving on. Start with SPI mode 0, MSB first, and 500 kHz to 1 MHz.

## Daemon Build

```sh
make
```

Outputs:

| Binary | Purpose |
| --- | --- |
| `sx1276-regversion` | Minimal SPI proof, reads register `0x42` |
| `meshcore-he4025` | Minimal OpenWrt userspace daemon with SPI/sysfs GPIO HAL |

Cross-compile by overriding the toolchain:

```sh
make CC=mips-openwrt-linux-gcc CXX=mips-openwrt-linux-g++
```

Install into a rootfs staging directory:

```sh
make DESTDIR=/path/to/rootfs install
```

## Full Firmware Replacement

Build a Dragino OpenWrt LEDE 18.06 sysupgrade image with the daemon, stats web
UI, and OTA support. This is the primary firmware path for the HE-4025 /
IBB-v1.0 target:

```sh
./scripts/build-firmware.sh
```

The build script auto-generates a version like
`meshcore-YYYYMMDDHHMM-SHORTSHA`. Equivalent explicit command with an override:

```sh
./scripts/build-dragino-lede-firmware.sh --version test1
```

See `docs/dragino-openwrt-lede-18.06.md` and
`docs/firmware-replacement.md` for the full replacement-image workflow. The
Linino/Yun builder is kept only as a legacy reference.

Pushes to `main` also run the GitHub Actions firmware workflow, create a
`firmware-meshcore-YYYYMMDDHHMM-SHORTSHA-runN` tag, and publish the generated
versioned sysupgrade image as a GitHub release asset.

## Runtime Config

Default config path:

```text
/etc/config/meshcore
```

The parser accepts either simple `key=value` lines or OpenWrt-style
`option key 'value'` lines.

There is also a Dragino IBB-v1.0 starting profile at
`boards/dragino-ibb-v1.0.conf`. Its GPIO values are placeholders until the
HE-4025 Linux GPIO numbers are confirmed.

For the 2x LoRa hardware variant, use
`boards/dragino-ibb-v1.0-dual-lora.conf`. See `docs/dual-lora-profile.md`.
You can bake it into the replacement firmware with:

```sh
./scripts/build-firmware.sh --board-profile dragino-ibb-v1.0-dual-lora
```

Example key/value config:

```text
role=repeater
region=AU915
frequency=915800000
spreading_factor=11
bandwidth=250000
coding_rate=5
tx_power=17
sync_word=0x12

spi_device=/dev/spidev0.0
spi_speed=1000000

pin_nss=-1
pin_reset=?
pin_dio0=?
pin_dio1=?
pin_dio2=?

poll_ms=5
repeat_raw=false
```

Use `pin_nss=-1` when the Linux SPI device handles chip-select. Set it to a
GPIO number only when NSS is wired to a separate GPIO; in that case the daemon
uses `SPI_NO_CS` and drives NSS itself.

`repeat_raw=false` is intentional. Leave it off until bench testing is controlled
and legal for your AU915 channel plan.

The daemon also includes a small HTTP UI:

```text
web_enabled=true
web_bind=0.0.0.0
web_port=8080
ota_enabled=false
ota_token=change-me
```

Open `http://DEVICE-IP:8080/` for live stats. OTA upload is rejected unless
`ota_enabled=true` and `ota_token` is changed to a real secret. The OTA endpoint
accepts a raw OpenWrt sysupgrade image, validates it with `/sbin/sysupgrade -T`,
then starts `/sbin/sysupgrade`.

## Useful Commands On The Box

Run these on the HE-4025 and fill in the missing GPIOs:

```sh
cat /etc/openwrt_release
uname -a
ls /dev/spidev*
ls /sys/class/gpio
dmesg | grep -i -E "spi|gpio|sx|lora"
```

Needed mappings:

```text
NSS/CS =
RESET =
DIO0 =
DIO1 =
```

For the Dragino IBB-v1.0, also confirm that the SX1276 is wired directly to the
HE-4025 SPI bus. Some Dragino IoT/gateway designs put the LoRa radio behind an
ATmega/Arduino-side MCU, which means `/dev/spidevX.Y` on Linux cannot read the
SX1276 directly. See `docs/dragino-ibb-v1.0.md`.

## Daemon Usage

Probe only:

```sh
meshcore-he4025 -c /etc/config/meshcore --probe
```

Receive raw LoRa frames:

```sh
meshcore-he4025 -c /etc/config/meshcore
```

Transmit one raw LoRa frame from hex bytes, then return to RX:

```sh
meshcore-he4025 -c /etc/config/meshcore --tx-hex 48656c6c6f
```

Stats API:

```sh
curl http://DEVICE-IP:8080/api/stats
```

OTA validation only:

```sh
curl -X POST \
  -H "X-OTA-Token: YOUR_TOKEN" \
  --data-binary @dragino-xxx-squashfs-sysupgrade.bin \
  "http://DEVICE-IP:8080/api/ota?validate=1"
```

OTA flash:

```sh
curl -X POST \
  -H "X-OTA-Token: YOUR_TOKEN" \
  --data-binary @dragino-xxx-squashfs-sysupgrade.bin \
  "http://DEVICE-IP:8080/api/ota"
```

## AU915 Reminder

Do not transmit continuously around 915 MHz. Use a legal AU915 channel plan,
sane TX power, and bench-safe RF practice such as low power and dummy loads
while testing.
