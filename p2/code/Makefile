CC = gcc
CFLAGS = -Wall
LDFLAGS =
OBJFILES = support.o thread-worker.o
TARGET = thread-worker

all: $(TARGET)

$(TARGET): $(OBJFILES)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJFILES) $(LDFLAGS)

clean:
	rm -f $(OBJFILES) $(TARGET) *~