CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -g -Og
LIB = -lm -ldl -llua 

SRCS = $(wildcard *.c)

OBJECTS = $(patsubst %.c,%.o,$(SRCS))


all: main

main: $(OBJECTS)
	$(CC) $(CFLAGS) -o main $(OBJECTS) $(LIB)  

.PHONY:deb
deb:
	@echo $(SRCS)
	@echo $(OBJECTS)
