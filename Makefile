CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -D_POSIX_C_SOURCE=200809L

.PHONY: all clean

all: parent child

parent: parent.c io.c io.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -fno-builtin $(LDFLAGS) -o $@ parent.c io.c

child: child.c numbers.c numbers.h io.c io.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -fno-builtin $(LDFLAGS) -o $@ child.c numbers.c io.c

clean:
	$(RM) parent child
