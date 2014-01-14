#include <pthread.h>
#include <netinet/in.h>

#define MAX_THREAD 512

typedef struct thread_data {
        int clientfd;
        pthread_t thread;
        struct sockaddr_in client;
        pthread_mutex_t *log_lock;
} thread_data;

int proxy_thr(int listenfd);
int proxy_proc(int listenfd);
int proxy_seq(int listenfd);

