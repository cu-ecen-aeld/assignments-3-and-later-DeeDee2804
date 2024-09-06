# Default compiler and compiler flags
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt

# Default Target
$(TARGET): aesdsocket.o
	$(CC) $(CFLAGS) aesdsocket.o -o $(TARGET) $(LDFLAGS)
aesdsocket.o: aesdsocket.c
	$(CC) $(CFLAGS) -c aesdsocket.c

all: $(TARGET)

clean:
	rm -f *.o $(TARGET)