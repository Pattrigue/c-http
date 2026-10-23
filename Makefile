CC      = cc
CFLAGS  = -Wall -Wextra # Common warnings + extra warnings
MODE    ?= debug


ifeq ($(MODE),release)
	# Enable O2 optimization in release
	CFLAGS += -O2
else
	# Enable debug info (-g) and runtime checks for memory bugs in debug mode
	CFLAGS += -g -fsanitize=address
endif


all: build/c-http

build/c-http: src/main.c src/http.h src/http.c | build
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

build:
	mkdir -p build

compdb: clean
	bear -- $(MAKE)

clean:
	rm -rf build

.PHONY: all clean compdb
