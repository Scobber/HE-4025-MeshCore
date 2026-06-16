#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

int main(int argc, char **argv) {
    const char *dev = argc > 1 ? argv[1] : "/dev/spidev0.0";

    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = 1000000;

    ioctl(fd, SPI_IOC_WR_MODE, &mode);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    uint8_t tx[2] = { 0x42 & 0x7F, 0x00 };
    uint8_t rx[2] = { 0 };

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = 2,
        .speed_hz = speed,
        .bits_per_word = bits,
    };

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
        perror("spi");
        close(fd);
        return 1;
    }

    printf("SX1276 RegVersion: 0x%02X\n", rx[1]);

    close(fd);
    return 0;
}

