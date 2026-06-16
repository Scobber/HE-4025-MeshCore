# Dragino OpenWrt LEDE 18.06 Firmware

Use this firmware path for the HE-4025 / Dragino IBB-v1.0 target.

Source:

```text
https://github.com/dragino/openwrt_lede-18.06
```

This is the primary replacement-firmware base for this repo. It is newer and
more complete than the legacy Arduino Yun/Linino tree.

## Build

On a Linux build machine with OpenWrt build dependencies installed:

```sh
./scripts/build-firmware.sh
```

The build auto-generates a version like `meshcore-YYYYMMDDHHMM-SHORTSHA`, which
Dragino's build script puts into the output image filenames. That wrapper calls:

```sh
./scripts/build-dragino-lede-firmware.sh
```

For serial failure logs:

```sh
./scripts/build-firmware.sh --single-thread
```

The script:

1. clones `https://github.com/dragino/openwrt_lede-18.06.git` into `build/openwrt_lede-18.06`
2. runs Dragino's `set_up_build_environment.sh` if feeds are not ready
3. copies this package to `openwrt/package/meshcore-he4025`
4. copies the firmware overlay to `files-meshcore-he4025`
5. creates `.config.meshcore-he4025` from Dragino's `.config.lgw`
6. appends `firmware/meshcore-he4025.config.append`
7. runs OpenWrt `make defconfig`
8. runs `./build_image.sh -a meshcore-he4025`

Output images land under:

```text
build/openwrt_lede-18.06/image/
```

Use the generated `*-squashfs-sysupgrade.bin` image for OTA and normal
OpenWrt sysupgrade.

## C++ Runtime

Dragino's `.config.lgw` prefers `uClibc++`. The package Makefile includes
`include/uclibc++.mk`, so the daemon depends on:

```text
+USE_UCLIBCXX:uclibcxx +USE_LIBSTDCXX:libstdcpp
```

The LEDE config fragment selects `uclibcxx` to match Dragino's default.

## Radio Bring-Up

The firmware image includes:

- `meshcore-he4025`
- `sx1276-regversion`
- `kmod-spi-dev`
- `/etc/config/meshcore`
- `/etc/init.d/meshcore`
- embedded stats and OTA web UI on port `8080`

Still prove SPI before treating MeshCore as the problem:

```sh
sx1276-regversion /dev/spidev0.0
```

Expected:

```text
SX1276 RegVersion: 0x12
```
