# Makefile - pTOS real-time demonstrator (a normal pTOS program)
#
# Uses the libcmini that the pTOS build produces (make test-hd in pTOS
# builds it once).  The result, STEPDEMO.TOS, goes onto the SD card.

PTOS     ?= ../../pTOS
LIBCMINI  = $(PTOS)/obj/libcmini-build

CC       = arm-none-eabi-gcc
CPUFLAGS = -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16
CFLAGS   = $(CPUFLAGS) -O2 -g -Wall -mword-relocations \
           -I$(PTOS)/lib/libcmini/include -iquote $(PTOS)/include
LDFLAGS  = $(CPUFLAGS) -nostartfiles -nostdlib \
           -Wl,-q -Wl,-Ttext=0 -Wl,-e_start

all: STEPDEMO.TOS

STEPDEMO.TOS: stepdemo.o
	$(CC) $(LDFLAGS) $(LIBCMINI)/objs/minicrt0.o stepdemo.o \
	    -L$(LIBCMINI) -lcmini -lgcc -o $@

stepdemo.o: stepdemo.c $(PTOS)/include/rtx.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o STEPDEMO.TOS

.PHONY: all clean
