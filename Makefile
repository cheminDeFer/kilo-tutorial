CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -g 
LIB = -lm -ldl -llua 

SRCS = $(wildcard *.c)

OBJECTS = $(patsubst %.c,%.o,$(SRCS))


all: kilo

kilo: $(OBJECTS)
	$(CC) $(CFLAGS) -o kilo $(OBJECTS) $(LIB)

.PHONY:deb
deb:
	@echo $(SRCS)
	@echo $(OBJECTS)
