CC=gcc
CFLAGS=-O2 -std=c11 -Wall -Wextra -pedantic
TARGET=bin/pipeline
SRC=src/pipeline.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	@if [ ! -d bin ]; then mkdir -p bin; fi
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -rf bin out
