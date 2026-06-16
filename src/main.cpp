#include "config.h"
#include "linux_hal.h"
#include "stats.h"
#include "sx1276.h"
#include "web_server.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sstream>
#include <vector>

namespace {

volatile sig_atomic_t g_running = 1;

void handle_signal(int) {
    g_running = 0;
}

void usage(const char *argv0) {
    printf("Usage: %s [-c /etc/config/meshcore] [--probe] [--tx-hex HEX]\n", argv0);
}

void record_error(DaemonStats *stats, const std::string &error) {
    stats->last_error = error;
    stats->last_error_ms = hal_millis();
}

bool parse_hex(const std::string &hex, std::vector<uint8_t> *out, std::string *error) {
    std::string clean;
    for (size_t i = 0; i < hex.size(); ++i) {
        const char c = hex[i];
        if (c == ':' || c == '-' || c == ' ' || c == '\t') {
            continue;
        }
        clean.push_back(c);
    }

    if (clean.empty() || (clean.size() % 2) != 0) {
        *error = "hex payload must contain an even number of digits";
        return false;
    }

    out->clear();
    for (size_t i = 0; i < clean.size(); i += 2) {
        char byte_text[3] = { clean[i], clean[i + 1], '\0' };
        char *end = NULL;
        const long value = strtol(byte_text, &end, 16);
        if (end == byte_text || *end != '\0' || value < 0 || value > 255) {
            *error = "invalid hex byte: " + std::string(byte_text);
            return false;
        }
        out->push_back(static_cast<uint8_t>(value));
    }
    return true;
}

std::string to_hex(const std::vector<uint8_t> &bytes) {
    static const char lut[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (size_t i = 0; i < bytes.size(); ++i) {
        out.push_back(lut[(bytes[i] >> 4) & 0x0f]);
        out.push_back(lut[bytes[i] & 0x0f]);
    }
    return out;
}

bool setup_gpio_if_needed(SysfsGpio *gpio, int pin, const std::string &direction,
                          const char *name) {
    if (pin < 0) {
        return true;
    }

    std::string error;
    if (!gpio->open_pin(pin, direction, &error)) {
        fprintf(stderr, "%s GPIO %d: %s\n", name, pin, error.c_str());
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    std::string config_path = "/etc/config/meshcore";
    bool probe_only = false;
    std::string tx_hex;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--probe") {
            probe_only = true;
        } else if (arg == "--tx-hex" && i + 1 < argc) {
            tx_hex = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    Config config = default_config();
    std::string error;
    if (!load_config_file(config_path, &config, &error)) {
        fprintf(stderr, "config error: %s\n", error.c_str());
        return 1;
    }

    std::vector<uint8_t> tx_payload;
    if (!tx_hex.empty() && !parse_hex(tx_hex, &tx_payload, &error)) {
        fprintf(stderr, "TX payload error: %s\n", error.c_str());
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    DaemonStats stats;
    stats.started_ms = hal_millis();

    WebServer web(&config, &stats);
    if (!web.start(&error)) {
        fprintf(stderr, "web server failed: %s\n", error.c_str());
        return 1;
    }
    if (config.web_enabled) {
        printf("web UI listening on http://%s:%d/\n", config.web_bind.c_str(), config.web_port);
    }

    SysfsGpio nss;
    SysfsGpio reset;
    SysfsGpio dio0;

    if (!setup_gpio_if_needed(&nss, config.pin_nss, "out", "NSS") ||
        !setup_gpio_if_needed(&reset, config.pin_reset, "out", "RESET") ||
        !setup_gpio_if_needed(&dio0, config.pin_dio0, "in", "DIO0")) {
        return 1;
    }

    if (config.pin_nss >= 0 && !nss.write_value(true, &error)) {
        fprintf(stderr, "NSS idle high failed: %s\n", error.c_str());
        return 1;
    }
    if (config.pin_reset >= 0 && !reset.write_value(true, &error)) {
        fprintf(stderr, "RESET idle high failed: %s\n", error.c_str());
        return 1;
    }

    SpiDevice spi;
    if (!spi.open_device(config.spi_device, config.spi_speed, config.pin_nss >= 0, &error)) {
        fprintf(stderr, "SPI open failed: %s\n", error.c_str());
        return 1;
    }

    Sx1276 radio(&spi, config.pin_nss >= 0 ? &nss : NULL, config.pin_reset >= 0 ? &reset : NULL);
    if (!radio.hardware_reset(&error)) {
        fprintf(stderr, "radio reset failed: %s\n", error.c_str());
        return 1;
    }

    uint8_t version = 0;
    if (!radio.read_version(&version, &error)) {
        record_error(&stats, error);
        fprintf(stderr, "read RegVersion failed: %s\n", error.c_str());
        return 1;
    }
    stats.spi_version_reads++;
    stats.sx1276_version = version;

    printf("SX1276 RegVersion: 0x%02X\n", version);
    if (version != 0x12) {
        fprintf(stderr, "expected 0x12; stop here and fix SPI wiring/CS/mode/speed\n");
        return 1;
    }
    if (probe_only) {
        return 0;
    }

    RadioConfig radio_config;
    radio_config.frequency = config.frequency;
    radio_config.spreading_factor = config.spreading_factor;
    radio_config.bandwidth = config.bandwidth;
    radio_config.coding_rate = config.coding_rate;
    radio_config.tx_power = config.tx_power;
    radio_config.sync_word = static_cast<uint8_t>(config.sync_word & 0xff);
    radio_config.preamble_symbols = 8;

    if (!radio.begin_lora(radio_config, &error)) {
        record_error(&stats, error);
        fprintf(stderr, "radio init failed: %s\n", error.c_str());
        return 1;
    }
    stats.radio_ready = true;

    if (!tx_payload.empty()) {
        printf("TX %zu bytes: %s\n", tx_payload.size(), to_hex(tx_payload).c_str());
        if (!radio.transmit_packet(tx_payload, &error)) {
            stats.tx_errors++;
            record_error(&stats, error);
            fprintf(stderr, "TX failed: %s\n", error.c_str());
            return 1;
        }
        stats.tx_packets++;
        stats.last_tx_ms = hal_millis();
        stats.last_tx_bytes = tx_payload.size();
        stats.last_tx_hex = to_hex(tx_payload);
    }

    if (!radio.rx_continuous(&error)) {
        record_error(&stats, error);
        fprintf(stderr, "RX continuous failed: %s\n", error.c_str());
        return 1;
    }
    stats.rx_continuous = true;

    printf("meshcore-he4025 raw LoRa daemon started: role=%s region=%s freq=%llu spi=%s speed=%u\n",
           config.role.c_str(), config.region.c_str(),
           static_cast<unsigned long long>(config.frequency), config.spi_device.c_str(),
           config.spi_speed);

    while (g_running) {
        web.tick();

        bool check_radio = config.pin_dio0 < 0;
        if (config.pin_dio0 >= 0) {
            bool dio0_high = false;
            if (!dio0.read_value(&dio0_high, &error)) {
                record_error(&stats, error);
                fprintf(stderr, "DIO0 read failed: %s\n", error.c_str());
                return 1;
            }
            check_radio = dio0_high;
        }

        if (check_radio) {
            RadioPacket packet;
            bool got_packet = false;
            if (!radio.receive_packet(&packet, &got_packet, &error)) {
                stats.rx_errors++;
                record_error(&stats, error);
                fprintf(stderr, "RX failed: %s\n", error.c_str());
                return 1;
            }
            if (got_packet) {
                stats.rx_packets++;
                stats.last_rx_ms = hal_millis();
                stats.last_rx_bytes = packet.payload.size();
                stats.last_rssi_dbm = packet.rssi_dbm;
                stats.last_snr_db = packet.snr_db;
                stats.last_rx_hex = to_hex(packet.payload);

                printf("RX %zu bytes RSSI=%d dBm SNR=%.1f dB HEX=%s\n", packet.payload.size(),
                       packet.rssi_dbm, packet.snr_db, to_hex(packet.payload).c_str());

                if (config.role == "repeater" && config.repeat_raw) {
                    printf("Repeating raw frame (%zu bytes)\n", packet.payload.size());
                    if (!radio.transmit_packet(packet.payload, &error)) {
                        stats.tx_errors++;
                        record_error(&stats, error);
                        fprintf(stderr, "repeat TX failed: %s\n", error.c_str());
                        return 1;
                    }
                    stats.tx_packets++;
                    stats.last_tx_ms = hal_millis();
                    stats.last_tx_bytes = packet.payload.size();
                    stats.last_tx_hex = to_hex(packet.payload);
                }
            }
        }

        hal_delay_ms(config.poll_ms == 0 ? 1 : config.poll_ms);
    }

    printf("meshcore-he4025 stopped\n");
    return 0;
}
