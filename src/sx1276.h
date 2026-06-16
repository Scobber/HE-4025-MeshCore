#ifndef MESHCORE_HE4025_SX1276_H
#define MESHCORE_HE4025_SX1276_H

#include <stdint.h>

#include <string>
#include <vector>

#include "linux_hal.h"

struct RadioConfig {
    uint64_t frequency;
    int spreading_factor;
    int bandwidth;
    int coding_rate;
    int tx_power;
    uint8_t sync_word;
    uint16_t preamble_symbols;
};

struct RadioPacket {
    std::vector<uint8_t> payload;
    int rssi_dbm;
    float snr_db;
};

class Sx1276 {
public:
    Sx1276(SpiDevice *spi, SysfsGpio *nss, SysfsGpio *reset);

    bool hardware_reset(std::string *error);
    bool read_version(uint8_t *version, std::string *error);
    bool begin_lora(const RadioConfig &config, std::string *error);
    bool rx_continuous(std::string *error);
    bool receive_packet(RadioPacket *packet, bool *got_packet, std::string *error);
    bool transmit_packet(const std::vector<uint8_t> &payload, std::string *error);

private:
    bool read_reg(uint8_t reg, uint8_t *value, std::string *error);
    bool write_reg(uint8_t reg, uint8_t value, std::string *error);
    bool read_fifo(std::vector<uint8_t> *payload, size_t len, std::string *error);
    bool write_fifo(const std::vector<uint8_t> &payload, std::string *error);
    bool transfer(uint8_t *tx, uint8_t *rx, size_t len, std::string *error);
    bool set_frequency(uint64_t frequency, std::string *error);
    bool set_modem_config(const RadioConfig &config, std::string *error);
    bool set_tx_power(int dbm, std::string *error);
    bool standby(std::string *error);
    bool sleep(std::string *error);
    bool clear_irq(uint8_t flags, std::string *error);

    SpiDevice *spi_;
    SysfsGpio *nss_;
    SysfsGpio *reset_;
};

#endif

