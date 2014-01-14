CC = gcc
CFLAGS = -Wall -g -pedantic
LDFLAGS = -lpthread -lrt

EXEC = proxy
SRCS = proxy.c launch.c iowrap.c http_helper.c http_method.c http.c slog.c shm.c
OBJS = ${SRCS:.c=.o}

all: $(EXEC)

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $<

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(EXEC) $(OBJS)

clean:
	rm -f $(EXEC) $(OBJS)

