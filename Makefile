CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -g 
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
