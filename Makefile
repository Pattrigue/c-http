CC      = cc
CFLAGS  = -Wall -Wextra # Common warnings + extra warnings
mode   ?= debug


ifeq ($(MODE),release)
	# Enable O2 optimization in release
	CFLAGS += -O2
else
	# Enable debug info (-g) and runtime checks for memory bugs in debug mode
	CFLAGS += -g -fsanitize=address
endif


all: main

main: main.c
	$(CC) $(CFLAGS) -o main main.c

clean:
	rm -f main

.PHONY: all clean
