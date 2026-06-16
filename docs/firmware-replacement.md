# Full Firmware Replacement

This repo can be built into a Dragino HE/DRAGINO2 sysupgrade image using
Dragino's OpenWrt LEDE 18.06 firmware tree:

```text
https://github.com/dragino/openwrt_lede-18.06
```

The image includes:

- `/usr/bin/meshcore-he4025`
- `/usr/bin/sx1276-regversion`
- `/etc/init.d/meshcore`
- `/etc/config/meshcore`
- embedded HTTP stats UI on port `8080`
- token-gated OTA upload through `/api/ota`

## Build

On a Linux build machine with OpenWrt build dependencies installed:

```sh
./scripts/build-firmware.sh
```

The build auto-generates a version like `meshcore-YYYYMMDDHHMM-SHORTSHA`, which
is passed to Dragino's `build_image.sh -v` and appears in the output image
filenames. Equivalent explicit command with an override:

```sh
./scripts/build-dragino-lede-firmware.sh --version test1
```

For easier LEDE failure logs:

```sh
./scripts/build-dragino-lede-firmware.sh --single-thread
```

The LEDE script clones Dragino's LEDE 18.06 SDK by default, copies this package into
`openwrt/package/meshcore-he4025`, creates a `files-meshcore-he4025` overlay,
and runs:

```sh
./build_image.sh -a meshcore-he4025
```

LEDE output images are written under:

```text
build/openwrt_lede-18.06/image/
```

Use the `*-squashfs-sysupgrade.bin` file for in-place OpenWrt upgrades.

The old `dragino/linino` builder remains in this repo only as a legacy reference
for Arduino Yun-era images.

## GitHub Releases

Pushing to `main` runs `.github/workflows/firmware-release.yml`. The workflow
builds the Dragino LEDE firmware, uploads the sysupgrade image as a workflow
artifact, creates a tag, and publishes a GitHub release.

Tags use this shape:

```text
firmware-meshcore-YYYYMMDDHHMM-SHORTSHA-runN
```

The release assets include the generated `*-squashfs-sysupgrade.bin` image,
`sha256sums` when Dragino's build produces it, and the standalone bring-up/test
pieces:

```text
meshcore-he4025-VERSION-linux-x86_64
sx1276-regversion-VERSION-linux-x86_64
meshcore-he4025-test-root-VERSION.tar.gz
```

Those host binaries are CI smoke-build artifacts. The flashable device payload
is still the Dragino `*-squashfs-sysupgrade.bin`.

## First Boot

The firmware overlay enables the daemon on first boot. Browse to:

```text
http://DEVICE-IP:8080/
```

Before OTA is accepted, edit `/etc/config/meshcore`:

```text
option ota_enabled 'true'
option ota_token 'replace-with-a-real-secret'
```

Then restart:

```sh
/etc/init.d/meshcore restart
```

## OTA

Validate only:

```sh
curl -X POST \
  -H "X-OTA-Token: replace-with-a-real-secret" \
  --data-binary @dragino-meshcore-he4025-test1-squashfs-sysupgrade.bin \
  "http://DEVICE-IP:8080/api/ota?validate=1"
```

Flash:

```sh
curl -X POST \
  -H "X-OTA-Token: replace-with-a-real-secret" \
  --data-binary @dragino-meshcore-he4025-test1-squashfs-sysupgrade.bin \
  "http://DEVICE-IP:8080/api/ota"
```

The daemon validates the image with `/sbin/sysupgrade -T` before it starts the
real upgrade. By default it uses `sysupgrade -n` so the replacement is clean.
Set `ota_keep_config=true` if you want to preserve OpenWrt config files.

## Hardware Gate

Do not flash repeatedly while the radio wiring is unknown. First prove the box
can read the SX1276 directly:

```sh
sx1276-regversion /dev/spidev0.0
```

Expected:

```text
SX1276 RegVersion: 0x12
```
