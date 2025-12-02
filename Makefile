# PROCX PROJESİ İÇİN MAKEFILE
# ----------------------------------------------------

CC      = gcc
CFLAGS  = -g -Wall -std=c99
LDFLAGS = -lrt -pthread

SRC     = procx.c
TARGET  = procx


all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) $(CFLAGS) $(LDFLAGS) -o $(TARGET)
clean:
	rm -f $(TARGET)