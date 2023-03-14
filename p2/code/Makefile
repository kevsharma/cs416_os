CC = gcc
CFLAGS = -g -std=c99 -Wvla -Wall # -pthread -fsanitize=address,undefined

all: thread-worker

thread-worker: thread-worker.c
	gcc -o thread-worker thread-worker.c


clean:
	rm thread-worker *.o *.a
