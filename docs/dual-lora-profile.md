# Dual LoRa IBB-v1.0 Profile

Use `boards/dragino-ibb-v1.0-dual-lora.conf` for the HE-4025 / IBB-v1.0
hardware variant with two SX1276 radios.

Build firmware with this profile baked into `/etc/config/meshcore`:

```sh
./scripts/build-firmware.sh --board-profile dragino-ibb-v1.0-dual-lora
```

The profile contains:

- primary daemon-compatible fields: `spi_device`, `pin_reset`, `pin_dio0`
- explicit future fields: `radio0_*` and `radio1_*`
- `radio_count=2`

The current daemon opens one SX1276 instance. The dual-radio profile is shaped so
the next runtime step can add a second `Sx1276` instance without changing the
deployed config file.

Before using it, fill in the real Linux GPIO numbers:

```text
radio0_spi_device =
radio0_pin_nss =
radio0_pin_reset =
radio0_pin_dio0 =
radio0_pin_dio1 =

radio1_spi_device =
radio1_pin_nss =
radio1_pin_reset =
radio1_pin_dio0 =
radio1_pin_dio1 =
```

Probe each radio independently:

```sh
sx1276-regversion /dev/spidev0.0
sx1276-regversion /dev/spidev0.1
```

Expected for both:

```text
SX1276 RegVersion: 0x12
```

If the second radio uses a GPIO-controlled NSS instead of a kernel SPI
chip-select, set `radio1_pin_nss` to that GPIO and expose the matching
`/dev/spidevX.Y` device with `SPI_NO_CS` semantics in the runtime.
