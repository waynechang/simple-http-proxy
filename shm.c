#include <unistd.h>
#include <errno.h>
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

#include "shm.h"

void *shmalloc(const char *name, size_t size)
{
    int fd;
    void *addr;

    fd = shm_open(name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        perror("shm_open");
        return NULL;
    }

    if (ftruncate(fd, size) == -1) {
        perror("ftruncate");
        return NULL;
    }

    addr = mmap(NULL, size,
                PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (addr == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }

    return addr;
}

void shfree(const char *name, void *addr, size_t size)
{
    if (munmap(addr, size) == -1)
        perror("munmap");

    if (shm_unlink(name) == -1)
        perror("shm_unlink");
}

