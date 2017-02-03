#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

#define DEVFILE "/dev/devone0"

int main(void)
{
    int fd;
    struct pollfd fds;
    int retval;
    unsigned char buf[64];
    ssize_t sz;
    int i;

    fd = open(DEVFILE, O_RDWR);
    if (fd == -1) {
        perror("open failed\n");
        exit(1);
    }

    do {
        fds.fd = fd;
        fds.events = POLLIN;
        printf("poll()...\n");
        retval = poll(&fds, 1, 30 * 1000);
        if (retval == -1) {
            perror("poll error");
            break;
        }
        if (retval)
            break;
    } while(retval == 0);

    if (fds.revents & POLLIN) {
        printf("read()...\n");
        sz = read(fd, buf, sizeof(buf));
        for (i = 0; i < sz; i++)
            printf("%02x", buf[i]);
        printf("\n");
    }
    return 0;
}
