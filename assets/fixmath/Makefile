CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -std=c99
LDFLAGS ?= -lm

OBJS = fix16.o fix16_sqrt.o fix16_trig.o test_fixmath.o

all: test_fixmath
	./test_fixmath

test_fixmath: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c fix16.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o test_fixmath

.PHONY: all clean
