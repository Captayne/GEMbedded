# Makefile - irktest: the multitasking interface, tried step by step
#
# An ordinary pTOS program, built against the libcmini that the pTOS
# build produces ("make test-hd" in pTOS builds it once).  The result,
# IRKTEST.TOS, goes onto the SD card or through the deploy terminal.

PTOS     ?= ../../pTOS
LIBCMINI  = $(PTOS)/obj/libcmini-build

CC       = arm-none-eabi-gcc
CPUFLAGS = -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16
CFLAGS   = $(CPUFLAGS) -O2 -g -Wall -Wextra -mword-relocations \
           -I$(PTOS)/lib/libcmini/include -iquote $(PTOS)/include
LDFLAGS  = $(CPUFLAGS) -nostartfiles -nostdlib \
           -Wl,-q -Wl,-Ttext=0 -Wl,-e_start

all: IRKTEST.TOS

IRKTEST.TOS: irktest.o
	$(CC) $(LDFLAGS) $(LIBCMINI)/objs/minicrt0.o irktest.o \
	    -L$(LIBCMINI) -lcmini -lgcc -o $@

irktest.o: irktest.c $(PTOS)/include/irk.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o IRKTEST.TOS

.PHONY: all clean
