#ifndef MESHCORE_HE4025_LINUX_HAL_H
#define MESHCORE_HE4025_LINUX_HAL_H

#include <stddef.h>
#include <stdint.h>

#include <string>

class SpiDevice {
public:
    SpiDevice();
    ~SpiDevice();

    bool open_device(const std::string &path, uint32_t speed_hz, bool manual_cs,
                     std::string *error);
    bool transfer(const uint8_t *tx, uint8_t *rx, size_t len, std::string *error);
    void close_device();

private:
    int fd_;
    uint32_t speed_hz_;
    uint8_t bits_;
};

class SysfsGpio {
public:
    SysfsGpio();
    ~SysfsGpio();

    bool open_pin(int pin, const std::string &direction, std::string *error);
    bool write_value(bool high, std::string *error) const;
    bool read_value(bool *high, std::string *error) const;
    int pin() const;

private:
    int pin_;
    std::string gpio_path_;
};

uint64_t hal_millis();
void hal_delay_ms(unsigned int ms);
bool hal_random_bytes(uint8_t *buffer, size_t len, std::string *error);

#endif

