# Dragino Linino/Yun Firmware Legacy Notes

This is not the primary firmware path for the HE-4025 / IBB-v1.0 target. Use
Dragino's OpenWrt LEDE 18.06 tree instead:

```text
https://github.com/dragino/openwrt_lede-18.06
```

The Linino tree is kept here only as a legacy reference for Arduino Yun-era
images.

Source:

```text
https://github.com/dragino/linino
```

That repository's README identifies it as the Dragino Yun image source for MS14
and HE firmware below `1.3.5`, and its `trunk/build_image.sh` builds
`linino-16M` images named like:

```text
dragino2-yun-APP-v1.3.5-squashfs-sysupgrade.bin
```

## SPI Reality Check

In the Linino tree, `trunk/target/linux/ar71xx/files/arch/mips/ath79/mach-linino.c`
registers the board flash through `ath79_register_m25p80(NULL)`, but it does not
register an extra `spidev` device for an SX1276 radio.

That means this replacement image includes the daemon and `kmod-spi-dev`, but
`/dev/spidevX.Y` for the LoRa radio is not guaranteed by firmware alone. Direct
Linux SPI needs one of these to be true:

- the IBB-v1.0 already exposes the SX1276 on an existing spidev node
- a Linino board-file patch registers the SX1276/spidev chip-select
- the SX1276 is rewired from the ATmega side to HE SPI pins

If the radio is behind the ATmega, the next firmware step is a UART radio
coprocessor HAL rather than the Linux SPI HAL.

## Build

On a Linux machine with the old OpenWrt/Linino build dependencies installed:

```sh
./scripts/build-dragino-linino-firmware.sh
```

The script:

1. clones `https://github.com/dragino/linino.git` into `build/linino`
2. copies this package to `trunk/package/meshcore-he4025`
3. copies the firmware overlay to `trunk/files-meshcore-he4025`
4. creates `trunk/.config.meshcore-he4025` from `trunk/.config.common`
5. runs `make defconfig`
6. runs `trunk/build_image.sh meshcore-he4025`

The sysupgrade image lands under:

```text
build/linino/trunk/image/meshcore-he4025-build--v1.3.5--DATE/
```

Use:

```text
dragino2-yun-meshcore-he4025-v1.3.5-squashfs-sysupgrade.bin
```

## Note On Newer Yun Sources

The `dragino/linino` README points newer source work to:

```text
https://github.com/dragino/openwrt-yun
```

This repo has two builders:

| Script | Use |
| --- | --- |
| `scripts/build-firmware.sh` | Primary Dragino OpenWrt LEDE 18.06 path |
| `scripts/build-dragino-lede-firmware.sh` | Explicit primary LEDE 18.06 builder |
| `scripts/build-dragino-linino-firmware.sh` | Legacy Yun/Linino reference only |
