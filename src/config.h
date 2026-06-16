#ifndef MESHCORE_HE4025_CONFIG_H
#define MESHCORE_HE4025_CONFIG_H

#include <stdint.h>

#include <string>

struct Config {
    std::string role;
    std::string region;
    uint64_t frequency;
    int spreading_factor;
    int bandwidth;
    int coding_rate;
    int tx_power;
    int sync_word;

    std::string spi_device;
    uint32_t spi_speed;

    int pin_nss;
    int pin_reset;
    int pin_dio0;
    int pin_dio1;
    int pin_dio2;

    unsigned int poll_ms;
    bool repeat_raw;

    std::string device_name;
    bool web_enabled;
    std::string web_bind;
    int web_port;

    bool ota_enabled;
    std::string ota_token;
    std::string ota_image_path;
    bool ota_keep_config;
    uint64_t ota_max_upload_bytes;
};

Config default_config();
bool load_config_file(const std::string &path, Config *config, std::string *error);

#endif
