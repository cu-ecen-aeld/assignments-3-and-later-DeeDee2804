# Default compiler and compiler flags
CC := gcc
CF := -Wall -Werror
# If Cross Compiler provided as argument
ifdef CROSS_COMPILE
	CC := $(CROSS_COMPILE)gcc
endif
# Executable
OUTPUT := aesdsocket

# Default Target
$(OUTPUT): aesdsocket.o
	$(CC) $(CF) aesdsocket.o -o $(OUTPUT) -lpthread
aesdsocket.o: aesdsocket.c
	$(CC) $(CF) -c aesdsocket.c

all: $(OUTPUT)

clean:
	rm -f *.o $(OUTPUT)