CC = gcc
CFLAGS = -g -c
AR = ar -rc
RANLIB = ranlib

all: clean thread-worker.a

thread-worker.a: thread-worker.o
	$(AR) libthread-worker.a thread-worker.o
	$(RANLIB) libthread-worker.a

thread-worker.o: thread-worker.h

ifeq ($(SCHED), PSJF)
	$(CC) -pthread $(CFLAGS) -DPSJF thread-worker.c
else ifeq ($(SCHED), MLFQ)
	$(CC) -pthread $(CFLAGS) -DMLFQ thread-worker.c
else
	echo "no such scheduling algorithm"
endif

clean:
	rm -rf testfile *.o *.a
