#ifndef MESHCORE_HE4025_STATS_H
#define MESHCORE_HE4025_STATS_H

#include <stdint.h>

#include <string>

struct DaemonStats {
    uint64_t started_ms;
    uint64_t last_rx_ms;
    uint64_t last_tx_ms;
    uint64_t last_error_ms;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t spi_version_reads;
    size_t last_rx_bytes;
    size_t last_tx_bytes;
    int last_rssi_dbm;
    float last_snr_db;
    uint8_t sx1276_version;
    bool radio_ready;
    bool rx_continuous;
    bool ota_in_progress;
    std::string last_rx_hex;
    std::string last_tx_hex;
    std::string last_error;

    DaemonStats()
        : started_ms(0),
          last_rx_ms(0),
          last_tx_ms(0),
          last_error_ms(0),
          rx_packets(0),
          tx_packets(0),
          rx_errors(0),
          tx_errors(0),
          spi_version_reads(0),
          last_rx_bytes(0),
          last_tx_bytes(0),
          last_rssi_dbm(0),
          last_snr_db(0.0f),
          sx1276_version(0),
          radio_ready(false),
          rx_continuous(false),
          ota_in_progress(false) {}
};

#endif

