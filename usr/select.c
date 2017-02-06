#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define DEVFILE "/dev/devone0"

int main(void)
{
    int fd;
    fd_set rfds;
    struct timeval tv;
    int retval;
    unsigned char buf[64];
    ssize_t sz;
    int i;

    fd = open(DEVFILE, O_RDWR);

    if (fd == -1) {
        perror("open file errror");
        exit(1);
    }

    do {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        printf("select() ...\n");
        retval = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (retval == -1) {
            perror("select errror");
            break;
        }

        if (retval)
            break;
    } while (retval == 0);

    if (FD_ISSET(fd, &rfds)) {
        printf("reading...\n");
        sz = read(fd, buf, sizeof(buf));
        for (i = 0; i < sz; i++)
            printf("%02x", buf[i]);
        printf("\n");
    }

    close(fd);
    return 0;
}
