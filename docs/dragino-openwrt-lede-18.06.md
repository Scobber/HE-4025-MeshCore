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

To build the 2x LoRa profile into `/etc/config/meshcore`:

```sh
./scripts/build-firmware.sh --board-profile dragino-ibb-v1.0-dual-lora
```

The script:

1. clones `https://github.com/dragino/openwrt_lede-18.06.git` into `build/openwrt_lede-18.06`
2. runs Dragino's `set_up_build_environment.sh` if feeds are not ready
3. copies this package to `openwrt/package/meshcore-he4025`
4. copies the firmware overlay to `files-meshcore-he4025`
5. installs the selected `boards/*.conf` profile as `/etc/config/meshcore`
6. creates `.config.meshcore-he4025` from Dragino's `.config.lgw`
7. appends `firmware/meshcore-he4025.config.append`
8. runs OpenWrt `make defconfig`
9. pre-downloads source tarballs with `make download`
10. runs `./build_image.sh -a meshcore-he4025`
11. verifies that a versioned `*-squashfs-sysupgrade.bin` was produced

During feed setup, Dragino's old OpenWrt feed installer may print warnings like:

```text
WARNING: No feed for package 'libc' found
WARNING: No feed for package 'libssp' found
WARNING: No feed for package 'librt' found
WARNING: No feed for package 'libpthread' found
WARNING: No feed for source package 'lua' found
```

Those packages are OpenWrt toolchain/base packages, not feed packages. The build
script suppresses only those known harmless Dragino/OpenWrt feed lines and
leaves other warnings visible.

The Dragino LEDE tree also has an old GCC prerequisite regex that accepts GCC
`4.8` through `9`, but rejects modern two-digit versions such as GCC `11` or
`12`. The build script patches `openwrt/include/prereq-build.mk` in the cloned
SDK so current GitHub runners pass the same compiler check without using
`FORCE=1`.

The host `m4` tool in this tree also trips over modern glibc on Ubuntu 22.04+
with:

```text
c-stack.c:55:26: error: missing binary operator before token "("
```

The build script copies an Ubuntu-derived `m4` patch into
`openwrt/tools/m4/patches` so host-tool compilation no longer depends on
`SIGSTKSZ` being a compile-time constant.

The Dragino tree also ships `openwrt/package/kernel/qmi-wwan-q/Makefile` with
obsolete dependency names like `cdc-wdm`, `usbcore`, and `usbnet`. The build
script rewrites those to `kmod-usb-wdm`, `kmod-usb-core`, and `kmod-usb-net`
so OpenWrt stops warning about missing packages during the target prereq stage.

The original LEDE source mirror can time out:

```text
curl: (28) Failed to connect to sources.lede-project.org port 443
```

The build script writes `openwrt/scripts/localmirrors` in the cloned SDK and
removes the hard-coded `sources.lede-project.org` fallback from
`openwrt/scripts/download.pl`:

```text
https://sources.openwrt.org
https://sources.cdn.openwrt.org
https://downloads.openwrt.org/sources
```

Dragino's `build_image.sh` can print `Build Fails` but still exit with status
`0`. The wrapper treats a missing versioned sysupgrade image as a build failure.
For parallel builds, it reruns `build_image.sh` with `-s` so the CI log shows
the first real compiler or package failure instead of failing later during
artifact collection.

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
