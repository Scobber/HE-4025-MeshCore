# Dragino IBB-v1.0 Notes

The IBB-v1.0 detail that matters most is whether the SX1276 is actually on the
HE-4025 Linux SPI bus.

Some Dragino LG01/IoT kit style designs place an ATmega between the HE Linux
module and the LoRa radio: the ATmega talks to the SX1276 over SPI, while the HE
module talks to the ATmega over UART. In that topology, this userspace daemon
cannot read `RegVersion` over `/dev/spidevX.Y`, because the SX1276 is not wired
to the HE SPI controller.

## Fast Bring-Up Decision

1. Check whether `/dev/spidev*` exists on the HE-4025.
2. Confirm continuity or schematic routing from the SX1276 pins to the HE-4025:

```text
SX1276 SCK  -> HE SPI SCK
SX1276 MISO -> HE SPI MISO
SX1276 MOSI -> HE SPI MOSI
SX1276 NSS  -> HE SPI CS or HE GPIO
SX1276 RESET -> HE GPIO
SX1276 DIO0 -> HE GPIO
```

3. Run:

```sh
sx1276-regversion /dev/spidev0.0
```

Expected:

```text
SX1276 RegVersion: 0x12
```

## If The Board Uses An ATmega Bridge

Direct HE Linux SPI is the wrong first port. Use one of these paths instead:

1. Rework or jumper the SX1276 SPI, NSS, RESET, and DIO0 pins to HE-4025 pins.
2. Replace the ATmega firmware with a small radio coprocessor protocol and write
   a UART HAL instead of the Linux SPI HAL.
3. Use the original Dragino single-channel gateway architecture as-is and put
   MeshCore logic on the MCU side.

For the direct-SPI MeshCore target in this repo, the required next data is still:

```text
spi_device =
NSS/CS =
RESET =
DIO0 =
DIO1 =
```

