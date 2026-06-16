#include "linux_hal.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <sstream>
#include <vector>

namespace {

std::string errno_message(const std::string &prefix) {
    return prefix + ": " + strerror(errno);
}

bool write_text_file(const std::string &path, const std::string &value, std::string *error) {
    const int fd = open(path.c_str(), O_WRONLY);
    if (fd < 0) {
        *error = errno_message("open " + path);
        return false;
    }

    const ssize_t written = write(fd, value.c_str(), value.size());
    if (written < 0 || static_cast<size_t>(written) != value.size()) {
        *error = errno_message("write " + path);
        close(fd);
        return false;
    }

    close(fd);
    return true;
}

bool path_exists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::string gpio_path_for_pin(int pin) {
    std::ostringstream path;
    path << "/sys/class/gpio/gpio" << pin;
    return path.str();
}

}  // namespace

SpiDevice::SpiDevice() : fd_(-1), speed_hz_(1000000), bits_(8) {}

SpiDevice::~SpiDevice() {
    close_device();
}

bool SpiDevice::open_device(const std::string &path, uint32_t speed_hz, bool manual_cs,
                            std::string *error) {
    close_device();

    fd_ = open(path.c_str(), O_RDWR);
    if (fd_ < 0) {
        *error = errno_message("open " + path);
        return false;
    }

    speed_hz_ = speed_hz;
    bits_ = 8;

    uint8_t mode = SPI_MODE_0;
    if (manual_cs) {
        mode |= SPI_NO_CS;
    }

    if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0) {
        *error = errno_message("SPI_IOC_WR_MODE");
        close_device();
        return false;
    }
    if (ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits_) < 0) {
        *error = errno_message("SPI_IOC_WR_BITS_PER_WORD");
        close_device();
        return false;
    }
    if (ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz_) < 0) {
        *error = errno_message("SPI_IOC_WR_MAX_SPEED_HZ");
        close_device();
        return false;
    }

    return true;
}

bool SpiDevice::transfer(const uint8_t *tx, uint8_t *rx, size_t len, std::string *error) {
    if (fd_ < 0) {
        *error = "SPI device is not open";
        return false;
    }

    struct spi_ioc_transfer tr;
    memset(&tr, 0, sizeof(tr));
    tr.tx_buf = reinterpret_cast<unsigned long>(tx);
    tr.rx_buf = reinterpret_cast<unsigned long>(rx);
    tr.len = len;
    tr.speed_hz = speed_hz_;
    tr.bits_per_word = bits_;

    if (ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) < 1) {
        *error = errno_message("SPI_IOC_MESSAGE");
        return false;
    }

    return true;
}

void SpiDevice::close_device() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

SysfsGpio::SysfsGpio() : pin_(-1) {}

SysfsGpio::~SysfsGpio() {}

bool SysfsGpio::open_pin(int pin, const std::string &direction, std::string *error) {
    pin_ = pin;
    gpio_path_ = gpio_path_for_pin(pin);

    if (!path_exists(gpio_path_)) {
        std::ostringstream pin_text;
        pin_text << pin;
        if (!write_text_file("/sys/class/gpio/export", pin_text.str(), error)) {
            if (errno != EBUSY && !path_exists(gpio_path_)) {
                return false;
            }
        }
        hal_delay_ms(50);
    }

    return write_text_file(gpio_path_ + "/direction", direction, error);
}

bool SysfsGpio::write_value(bool high, std::string *error) const {
    if (pin_ < 0) {
        *error = "GPIO pin is not open";
        return false;
    }
    return write_text_file(gpio_path_ + "/value", high ? "1" : "0", error);
}

bool SysfsGpio::read_value(bool *high, std::string *error) const {
    if (pin_ < 0) {
        *error = "GPIO pin is not open";
        return false;
    }

    const std::string path = gpio_path_ + "/value";
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        *error = errno_message("open " + path);
        return false;
    }

    char value = '0';
    const ssize_t n = read(fd, &value, 1);
    close(fd);
    if (n != 1) {
        *error = errno_message("read " + path);
        return false;
    }

    *high = value != '0';
    return true;
}

int SysfsGpio::pin() const {
    return pin_;
}

uint64_t hal_millis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
           static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
}

void hal_delay_ms(unsigned int ms) {
    struct timespec req;
    req.tv_sec = ms / 1000U;
    req.tv_nsec = static_cast<long>(ms % 1000U) * 1000000L;
    while (nanosleep(&req, &req) < 0 && errno == EINTR) {
    }
}

bool hal_random_bytes(uint8_t *buffer, size_t len, std::string *error) {
    const int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        *error = errno_message("open /dev/urandom");
        return false;
    }

    size_t offset = 0;
    while (offset < len) {
        const ssize_t n = read(fd, buffer + offset, len - offset);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            *error = errno_message("read /dev/urandom");
            close(fd);
            return false;
        }
        if (n == 0) {
            *error = "short read from /dev/urandom";
            close(fd);
            return false;
        }
        offset += static_cast<size_t>(n);
    }

    close(fd);
    return true;
}

