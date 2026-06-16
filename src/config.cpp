#include "config.h"

#include <errno.h>
#include <stdlib.h>

#include <fstream>
#include <map>
#include <sstream>

namespace {

std::string trim(const std::string &input) {
    const char *ws = " \t\r\n";
    const std::string::size_type first = input.find_first_not_of(ws);
    if (first == std::string::npos) {
        return "";
    }
    const std::string::size_type last = input.find_last_not_of(ws);
    return input.substr(first, last - first + 1);
}

std::string strip_quotes(const std::string &input) {
    if (input.size() >= 2) {
        const char first = input.front();
        const char last = input.back();
        if ((first == '\'' && last == '\'') || (first == '"' && last == '"')) {
            return input.substr(1, input.size() - 2);
        }
    }
    return input;
}

bool parse_int64(const std::string &text, int64_t *out) {
    const std::string value = trim(strip_quotes(text));
    if (value.empty() || value == "?") {
        *out = -1;
        return true;
    }

    errno = 0;
    char *end = NULL;
    const long long parsed = strtoll(value.c_str(), &end, 0);
    if (errno != 0 || end == value.c_str() || *end != '\0') {
        return false;
    }
    *out = parsed;
    return true;
}

bool parse_uint64(const std::string &text, uint64_t *out) {
    int64_t parsed = 0;
    if (!parse_int64(text, &parsed) || parsed < 0) {
        return false;
    }
    *out = static_cast<uint64_t>(parsed);
    return true;
}

bool parse_bool(const std::string &text, bool *out) {
    const std::string value = trim(strip_quotes(text));
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        *out = true;
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        *out = false;
        return true;
    }
    return false;
}

bool parse_openwrt_option(const std::string &line, std::string *key, std::string *value) {
    std::istringstream in(line);
    std::string word;
    in >> word;
    if (word != "option") {
        return false;
    }
    if (!(in >> *key)) {
        return false;
    }
    std::string rest;
    std::getline(in, rest);
    *value = trim(rest);
    *value = strip_quotes(*value);
    return true;
}

bool parse_assignment(const std::string &line, std::string *key, std::string *value) {
    const std::string::size_type eq = line.find('=');
    if (eq == std::string::npos) {
        return false;
    }
    *key = trim(line.substr(0, eq));
    *value = strip_quotes(trim(line.substr(eq + 1)));
    return !key->empty();
}

bool set_config_value(Config *config, const std::string &key, const std::string &value,
                      std::string *error) {
    int64_t signed_value = 0;
    uint64_t unsigned_value = 0;
    bool bool_value = false;

    if (key == "role") {
        config->role = value;
    } else if (key == "region") {
        config->region = value;
    } else if (key == "frequency") {
        if (!parse_uint64(value, &unsigned_value)) {
            *error = "invalid frequency: " + value;
            return false;
        }
        config->frequency = unsigned_value;
    } else if (key == "spreading_factor") {
        if (!parse_int64(value, &signed_value)) {
            *error = "invalid spreading_factor: " + value;
            return false;
        }
        config->spreading_factor = static_cast<int>(signed_value);
    } else if (key == "bandwidth") {
        if (!parse_int64(value, &signed_value)) {
            *error = "invalid bandwidth: " + value;
            return false;
        }
        config->bandwidth = static_cast<int>(signed_value);
    } else if (key == "coding_rate") {
        if (!parse_int64(value, &signed_value)) {
            *error = "invalid coding_rate: " + value;
            return false;
        }
        config->coding_rate = static_cast<int>(signed_value);
    } else if (key == "tx_power") {
        if (!parse_int64(value, &signed_value)) {
            *error = "invalid tx_power: " + value;
            return false;
        }
        config->tx_power = static_cast<int>(signed_value);
    } else if (key == "sync_word") {
        if (!parse_int64(value, &signed_value)) {
            *error = "invalid sync_word: " + value;
            return false;
        }
        config->sync_word = static_cast<int>(signed_value);
    } else if (key == "spi_device") {
        config->spi_device = value;
    } else if (key == "spi_speed") {
        if (!parse_uint64(value, &unsigned_value)) {
            *error = "invalid spi_speed: " + value;
            return false;
        }
        config->spi_speed = static_cast<uint32_t>(unsigned_value);
    } else if (key == "pin_nss") {
        if (!parse_int64(value, &signed_value)) {
            *error = "invalid pin_nss: " + value;
            return false;
        }
        config->pin_nss = static_cast<int>(signed_value);
    } else if (key == "pin_reset") {
        if (!parse_int64(value, &signed_value)) {
            *error = "invalid pin_reset: " + value;
            return false;
        }
        config->pin_reset = static_cast<int>(signed_value);
    } else if (key == "pin_dio0") {
        if (!parse_int64(value, &signed_value)) {
            *error = "invalid pin_dio0: " + value;
            return false;
        }
        config->pin_dio0 = static_cast<int>(signed_value);
    } else if (key == "pin_dio1") {
        if (!parse_int64(value, &signed_value)) {
            *error = "invalid pin_dio1: " + value;
            return false;
        }
        config->pin_dio1 = static_cast<int>(signed_value);
    } else if (key == "pin_dio2") {
        if (!parse_int64(value, &signed_value)) {
            *error = "invalid pin_dio2: " + value;
            return false;
        }
        config->pin_dio2 = static_cast<int>(signed_value);
    } else if (key == "poll_ms") {
        if (!parse_uint64(value, &unsigned_value)) {
            *error = "invalid poll_ms: " + value;
            return false;
        }
        config->poll_ms = static_cast<unsigned int>(unsigned_value);
    } else if (key == "repeat_raw") {
        if (!parse_bool(value, &bool_value)) {
            *error = "invalid repeat_raw: " + value;
            return false;
        }
        config->repeat_raw = bool_value;
    } else if (key == "device_name") {
        config->device_name = value;
    } else if (key == "web_enabled") {
        if (!parse_bool(value, &bool_value)) {
            *error = "invalid web_enabled: " + value;
            return false;
        }
        config->web_enabled = bool_value;
    } else if (key == "web_bind") {
        config->web_bind = value;
    } else if (key == "web_port") {
        if (!parse_int64(value, &signed_value)) {
            *error = "invalid web_port: " + value;
            return false;
        }
        if (signed_value < 1 || signed_value > 65535) {
            *error = "web_port must be 1..65535";
            return false;
        }
        config->web_port = static_cast<int>(signed_value);
    } else if (key == "ota_enabled") {
        if (!parse_bool(value, &bool_value)) {
            *error = "invalid ota_enabled: " + value;
            return false;
        }
        config->ota_enabled = bool_value;
    } else if (key == "ota_token") {
        config->ota_token = value;
    } else if (key == "ota_image_path") {
        config->ota_image_path = value;
    } else if (key == "ota_keep_config") {
        if (!parse_bool(value, &bool_value)) {
            *error = "invalid ota_keep_config: " + value;
            return false;
        }
        config->ota_keep_config = bool_value;
    } else if (key == "ota_max_upload_bytes") {
        if (!parse_uint64(value, &unsigned_value)) {
            *error = "invalid ota_max_upload_bytes: " + value;
            return false;
        }
        config->ota_max_upload_bytes = unsigned_value;
    }

    return true;
}

}  // namespace

Config default_config() {
    Config config;
    config.role = "repeater";
    config.region = "AU915";
    config.frequency = 915800000ULL;
    config.spreading_factor = 11;
    config.bandwidth = 250000;
    config.coding_rate = 5;
    config.tx_power = 17;
    config.sync_word = 0x12;
    config.spi_device = "/dev/spidev0.0";
    config.spi_speed = 1000000;
    config.pin_nss = -1;
    config.pin_reset = -1;
    config.pin_dio0 = -1;
    config.pin_dio1 = -1;
    config.pin_dio2 = -1;
    config.poll_ms = 5;
    config.repeat_raw = false;
    config.device_name = "meshcore-he4025";
    config.web_enabled = true;
    config.web_bind = "0.0.0.0";
    config.web_port = 8080;
    config.ota_enabled = false;
    config.ota_token = "";
    config.ota_image_path = "/tmp/meshcore-sysupgrade.bin";
    config.ota_keep_config = false;
    config.ota_max_upload_bytes = 16ULL * 1024ULL * 1024ULL;
    return config;
}

bool load_config_file(const std::string &path, Config *config, std::string *error) {
    std::ifstream file(path.c_str());
    if (!file.good()) {
        return true;
    }

    std::string line;
    unsigned int line_no = 0;
    while (std::getline(file, line)) {
        ++line_no;
        const std::string::size_type comment = line.find('#');
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }
        line = trim(line);
        if (line.empty() || line.find("config ") == 0) {
            continue;
        }

        std::string key;
        std::string value;
        if (!parse_openwrt_option(line, &key, &value) && !parse_assignment(line, &key, &value)) {
            std::ostringstream msg;
            msg << path << ":" << line_no << ": unsupported config line";
            *error = msg.str();
            return false;
        }

        if (!set_config_value(config, key, value, error)) {
            std::ostringstream msg;
            msg << path << ":" << line_no << ": " << *error;
            *error = msg.str();
            return false;
        }
    }

    return true;
}
