#ifndef SHM_H
#define SHM_H

/* simple implementation to get started.
 * only safe before forking. */
void *shmalloc(const char *name, size_t size);
void shfree(const char *name, void *addr, size_t size);

#endif

