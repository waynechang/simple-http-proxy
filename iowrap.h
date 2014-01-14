#include <unistd.h>

#define MAX_BUF 8192
#define IOWRAP_READ 0
#define IOWRAP_WRITE 1

ssize_t readn(int fd, void *buf, size_t cnt);
ssize_t writen(int fd, const void *buf, size_t cnt);
ssize_t transfern(int srcfd, int destfd, size_t cnt);

int fdtunnel(int srcfd, int destfd);

#define waitread(fd) waitready(fd, IOWRAP_READ)
#define waitwrite(fd) waitready(fd, IOWRAP_WRITE)

int waitready(int fd, int type);

