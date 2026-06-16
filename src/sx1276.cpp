#include "sx1276.h"

#include <math.h>
#include <stdio.h>

#include <algorithm>

namespace {

const uint8_t REG_FIFO = 0x00;
const uint8_t REG_OP_MODE = 0x01;
const uint8_t REG_FR_MSB = 0x06;
const uint8_t REG_FR_MID = 0x07;
const uint8_t REG_FR_LSB = 0x08;
const uint8_t REG_PA_CONFIG = 0x09;
const uint8_t REG_LNA = 0x0C;
const uint8_t REG_FIFO_ADDR_PTR = 0x0D;
const uint8_t REG_FIFO_TX_BASE_ADDR = 0x0E;
const uint8_t REG_FIFO_RX_BASE_ADDR = 0x0F;
const uint8_t REG_FIFO_RX_CURRENT_ADDR = 0x10;
const uint8_t REG_IRQ_FLAGS = 0x12;
const uint8_t REG_RX_NB_BYTES = 0x13;
const uint8_t REG_PKT_SNR_VALUE = 0x19;
const uint8_t REG_PKT_RSSI_VALUE = 0x1A;
const uint8_t REG_MODEM_CONFIG_1 = 0x1D;
const uint8_t REG_MODEM_CONFIG_2 = 0x1E;
const uint8_t REG_PREAMBLE_MSB = 0x20;
const uint8_t REG_PREAMBLE_LSB = 0x21;
const uint8_t REG_PAYLOAD_LENGTH = 0x22;
const uint8_t REG_MODEM_CONFIG_3 = 0x26;
const uint8_t REG_DETECT_OPTIMIZE = 0x31;
const uint8_t REG_DETECTION_THRESHOLD = 0x37;
const uint8_t REG_SYNC_WORD = 0x39;
const uint8_t REG_DIO_MAPPING_1 = 0x40;
const uint8_t REG_VERSION = 0x42;

const uint8_t MODE_LONG_RANGE = 0x80;
const uint8_t MODE_SLEEP = 0x00;
const uint8_t MODE_STDBY = 0x01;
const uint8_t MODE_TX = 0x03;
const uint8_t MODE_RX_CONTINUOUS = 0x05;

const uint8_t IRQ_RX_DONE = 0x40;
const uint8_t IRQ_PAYLOAD_CRC_ERROR = 0x20;
const uint8_t IRQ_TX_DONE = 0x08;

bool bandwidth_to_reg(int bandwidth, uint8_t *reg) {
    switch (bandwidth) {
    case 7800:
    case 7810:
        *reg = 0;
        return true;
    case 10400:
    case 10420:
        *reg = 1;
        return true;
    case 15600:
    case 15625:
        *reg = 2;
        return true;
    case 20800:
    case 20830:
        *reg = 3;
        return true;
    case 31250:
        *reg = 4;
        return true;
    case 41700:
    case 41670:
        *reg = 5;
        return true;
    case 62500:
        *reg = 6;
        return true;
    case 125000:
        *reg = 7;
        return true;
    case 250000:
        *reg = 8;
        return true;
    case 500000:
        *reg = 9;
        return true;
    default:
        return false;
    }
}

}  // namespace

Sx1276::Sx1276(SpiDevice *spi, SysfsGpio *nss, SysfsGpio *reset)
    : spi_(spi), nss_(nss), reset_(reset) {}

bool Sx1276::hardware_reset(std::string *error) {
    if (reset_ == NULL || reset_->pin() < 0) {
        return true;
    }

    if (!reset_->write_value(false, error)) {
        return false;
    }
    hal_delay_ms(10);
    if (!reset_->write_value(true, error)) {
        return false;
    }
    hal_delay_ms(10);
    return true;
}

bool Sx1276::read_version(uint8_t *version, std::string *error) {
    return read_reg(REG_VERSION, version, error);
}

bool Sx1276::begin_lora(const RadioConfig &config, std::string *error) {
    if (!sleep(error)) {
        return false;
    }
    hal_delay_ms(10);

    uint8_t version = 0;
    if (!read_version(&version, error)) {
        return false;
    }
    if (version != 0x12) {
        char message[80];
        snprintf(message, sizeof(message), "unexpected SX1276 RegVersion 0x%02X", version);
        *error = message;
        return false;
    }

    if (!set_frequency(config.frequency, error)) {
        return false;
    }
    if (!write_reg(REG_FIFO_TX_BASE_ADDR, 0x00, error) ||
        !write_reg(REG_FIFO_RX_BASE_ADDR, 0x00, error)) {
        return false;
    }

    uint8_t lna = 0;
    if (!read_reg(REG_LNA, &lna, error)) {
        return false;
    }
    if (!write_reg(REG_LNA, lna | 0x03, error)) {
        return false;
    }

    if (!set_modem_config(config, error)) {
        return false;
    }
    if (!set_tx_power(config.tx_power, error)) {
        return false;
    }
    if (!write_reg(REG_SYNC_WORD, config.sync_word, error)) {
        return false;
    }
    if (!write_reg(REG_PREAMBLE_MSB, static_cast<uint8_t>(config.preamble_symbols >> 8), error) ||
        !write_reg(REG_PREAMBLE_LSB, static_cast<uint8_t>(config.preamble_symbols & 0xff), error)) {
        return false;
    }

    return standby(error);
}

bool Sx1276::rx_continuous(std::string *error) {
    if (!standby(error)) {
        return false;
    }
    if (!write_reg(REG_DIO_MAPPING_1, 0x00, error)) {
        return false;
    }
    if (!clear_irq(0xff, error)) {
        return false;
    }
    if (!write_reg(REG_FIFO_ADDR_PTR, 0x00, error)) {
        return false;
    }
    return write_reg(REG_OP_MODE, MODE_LONG_RANGE | MODE_RX_CONTINUOUS, error);
}

bool Sx1276::receive_packet(RadioPacket *packet, bool *got_packet, std::string *error) {
    *got_packet = false;

    uint8_t irq = 0;
    if (!read_reg(REG_IRQ_FLAGS, &irq, error)) {
        return false;
    }

    if ((irq & IRQ_PAYLOAD_CRC_ERROR) != 0) {
        clear_irq(irq, error);
        return true;
    }
    if ((irq & IRQ_RX_DONE) == 0) {
        return true;
    }

    uint8_t current_addr = 0;
    uint8_t length = 0;
    uint8_t raw_snr = 0;
    uint8_t raw_rssi = 0;
    if (!read_reg(REG_FIFO_RX_CURRENT_ADDR, &current_addr, error) ||
        !read_reg(REG_RX_NB_BYTES, &length, error) ||
        !read_reg(REG_PKT_SNR_VALUE, &raw_snr, error) ||
        !read_reg(REG_PKT_RSSI_VALUE, &raw_rssi, error)) {
        return false;
    }
    if (!write_reg(REG_FIFO_ADDR_PTR, current_addr, error)) {
        return false;
    }

    packet->payload.clear();
    if (!read_fifo(&packet->payload, length, error)) {
        return false;
    }

    const int8_t signed_snr = static_cast<int8_t>(raw_snr);
    packet->snr_db = static_cast<float>(signed_snr) / 4.0f;
    packet->rssi_dbm = static_cast<int>(raw_rssi) - 157;
    if (packet->snr_db < 0.0f) {
        packet->rssi_dbm += static_cast<int>(packet->snr_db);
    }

    if (!clear_irq(irq, error)) {
        return false;
    }

    *got_packet = true;
    return true;
}

bool Sx1276::transmit_packet(const std::vector<uint8_t> &payload, std::string *error) {
    if (payload.empty() || payload.size() > 255) {
        *error = "payload length must be 1..255 bytes";
        return false;
    }

    if (!standby(error)) {
        return false;
    }
    if (!write_reg(REG_DIO_MAPPING_1, 0x40, error)) {
        return false;
    }
    if (!clear_irq(0xff, error)) {
        return false;
    }
    if (!write_reg(REG_FIFO_ADDR_PTR, 0x00, error)) {
        return false;
    }
    if (!write_fifo(payload, error)) {
        return false;
    }
    if (!write_reg(REG_PAYLOAD_LENGTH, static_cast<uint8_t>(payload.size()), error)) {
        return false;
    }
    if (!write_reg(REG_OP_MODE, MODE_LONG_RANGE | MODE_TX, error)) {
        return false;
    }

    const uint64_t deadline = hal_millis() + 5000;
    while (hal_millis() < deadline) {
        uint8_t irq = 0;
        if (!read_reg(REG_IRQ_FLAGS, &irq, error)) {
            return false;
        }
        if ((irq & IRQ_TX_DONE) != 0) {
            if (!clear_irq(irq, error)) {
                return false;
            }
            return rx_continuous(error);
        }
        hal_delay_ms(2);
    }

    *error = "TX timed out waiting for TxDone";
    return false;
}

bool Sx1276::read_reg(uint8_t reg, uint8_t *value, std::string *error) {
    uint8_t tx[2] = { static_cast<uint8_t>(reg & 0x7f), 0x00 };
    uint8_t rx[2] = { 0x00, 0x00 };
    if (!transfer(tx, rx, sizeof(tx), error)) {
        return false;
    }
    *value = rx[1];
    return true;
}

bool Sx1276::write_reg(uint8_t reg, uint8_t value, std::string *error) {
    uint8_t tx[2] = { static_cast<uint8_t>(reg | 0x80), value };
    uint8_t rx[2] = { 0x00, 0x00 };
    return transfer(tx, rx, sizeof(tx), error);
}

bool Sx1276::read_fifo(std::vector<uint8_t> *payload, size_t len, std::string *error) {
    std::vector<uint8_t> tx(len + 1, 0x00);
    std::vector<uint8_t> rx(len + 1, 0x00);
    tx[0] = REG_FIFO & 0x7f;
    if (!transfer(&tx[0], &rx[0], tx.size(), error)) {
        return false;
    }
    payload->assign(rx.begin() + 1, rx.end());
    return true;
}

bool Sx1276::write_fifo(const std::vector<uint8_t> &payload, std::string *error) {
    std::vector<uint8_t> tx(payload.size() + 1, 0x00);
    std::vector<uint8_t> rx(payload.size() + 1, 0x00);
    tx[0] = REG_FIFO | 0x80;
    std::copy(payload.begin(), payload.end(), tx.begin() + 1);
    return transfer(&tx[0], &rx[0], tx.size(), error);
}

bool Sx1276::transfer(uint8_t *tx, uint8_t *rx, size_t len, std::string *error) {
    if (nss_ != NULL && nss_->pin() >= 0) {
        if (!nss_->write_value(false, error)) {
            return false;
        }
    }

    const bool ok = spi_->transfer(tx, rx, len, error);

    if (nss_ != NULL && nss_->pin() >= 0) {
        std::string nss_error;
        if (!nss_->write_value(true, &nss_error) && ok) {
            *error = nss_error;
            return false;
        }
    }

    return ok;
}

bool Sx1276::set_frequency(uint64_t frequency, std::string *error) {
    const uint64_t frf = (frequency << 19) / 32000000ULL;
    return write_reg(REG_FR_MSB, static_cast<uint8_t>((frf >> 16) & 0xff), error) &&
           write_reg(REG_FR_MID, static_cast<uint8_t>((frf >> 8) & 0xff), error) &&
           write_reg(REG_FR_LSB, static_cast<uint8_t>(frf & 0xff), error);
}

bool Sx1276::set_modem_config(const RadioConfig &config, std::string *error) {
    if (config.spreading_factor < 6 || config.spreading_factor > 12) {
        *error = "spreading_factor must be 6..12";
        return false;
    }
    if (config.coding_rate < 5 || config.coding_rate > 8) {
        *error = "coding_rate must be 5..8";
        return false;
    }

    uint8_t bw = 0;
    if (!bandwidth_to_reg(config.bandwidth, &bw)) {
        *error = "unsupported LoRa bandwidth";
        return false;
    }

    const uint8_t cr = static_cast<uint8_t>(config.coding_rate - 4);
    const uint8_t modem_config_1 = static_cast<uint8_t>((bw << 4) | (cr << 1));
    const uint8_t modem_config_2 = static_cast<uint8_t>((config.spreading_factor << 4) | 0x04);

    const double symbol_seconds =
        static_cast<double>(1UL << config.spreading_factor) / static_cast<double>(config.bandwidth);
    const uint8_t modem_config_3 = static_cast<uint8_t>(0x04 | (symbol_seconds > 0.016 ? 0x08 : 0x00));

    if (!write_reg(REG_MODEM_CONFIG_1, modem_config_1, error) ||
        !write_reg(REG_MODEM_CONFIG_2, modem_config_2, error) ||
        !write_reg(REG_MODEM_CONFIG_3, modem_config_3, error)) {
        return false;
    }

    if (config.spreading_factor == 6) {
        return write_reg(REG_DETECT_OPTIMIZE, 0x05, error) &&
               write_reg(REG_DETECTION_THRESHOLD, 0x0c, error);
    }

    return write_reg(REG_DETECT_OPTIMIZE, 0x03, error) &&
           write_reg(REG_DETECTION_THRESHOLD, 0x0a, error);
}

bool Sx1276::set_tx_power(int dbm, std::string *error) {
    if (dbm < 2) {
        dbm = 2;
    }
    if (dbm > 17) {
        dbm = 17;
    }
    return write_reg(REG_PA_CONFIG, static_cast<uint8_t>(0x80 | (dbm - 2)), error);
}

bool Sx1276::standby(std::string *error) {
    return write_reg(REG_OP_MODE, MODE_LONG_RANGE | MODE_STDBY, error);
}

bool Sx1276::sleep(std::string *error) {
    return write_reg(REG_OP_MODE, MODE_LONG_RANGE | MODE_SLEEP, error);
}

bool Sx1276::clear_irq(uint8_t flags, std::string *error) {
    return write_reg(REG_IRQ_FLAGS, flags, error);
}

