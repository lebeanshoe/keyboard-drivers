CC = gcc
CFLAGS = -std=gnu11 -g -Wall -I/usr/include/libusb-1.0
LDFLAGS = -lusb-1.0

SOURCES = driver.c
OBJS = $(SOURCES:.c=.o)
TARGET = driver

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

driver.o: driver.c
	$(CC) $(CFLAGS) -c -o driver.o driver.c

.PHONY: clean

clean:
	rm $(TARGET) $(OBJS)