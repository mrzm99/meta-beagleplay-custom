#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define I2C_BUS     "/dev/i2c-3"
#define I2C_ADDR    0x42

int main(void)
{
    int file;
    char buff[256];

    if ((file = open(I2C_BUS, O_RDWR)) < 0) {
        perror("Failed to open I2C bus");
        return 1;
    }

    if (ioctl(file, I2C_SLAVE, I2C_ADDR) < 0) {
        perror("Failed to acquire bus access");
        return 1;
    }

    printf("Starting GNSS NMEA read over I2C...\n");

    while (1) {
        buff[0] = 0xFF;
        if (write(file, buff, 1) != 1) {
            continue;
        }

        if (read(file, buff, 255) > 0) {
            for (int i = 0; i < 255; i++) {
                if (buff[i] != (char)0xFF && buff[i] != 0x00) {
                    putchar(buff[i]);
                }
            }
        }
        usleep(100000);
    }

    close(file);
    return 0;
}
