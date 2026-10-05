# Makefile - GEMSET.PRG, reads and changes what the machine was told it is
#
# A plain TOS program: it writes with printf and waits for a key, so it
# runs from the desktop and leaves its transcript on the screen.

PTOS     ?= ../../../pTOS
LIBCMINI  = $(PTOS)/obj/libcmini-build

CC       = arm-none-eabi-gcc
CPUFLAGS = -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16
CFLAGS   = $(CPUFLAGS) -O2 -g -Wall -mword-relocations \
           -I$(PTOS)/lib/libcmini/include \
           -I$(PTOS)/lib/libcmini/include/mint \
           -iquote $(PTOS)/include -iquote $(PTOS)/include/machine/rp2350
LDFLAGS  = $(CPUFLAGS) -nostartfiles -nostdlib \
           -Wl,-q -Wl,-Ttext=0 -Wl,-e_start

all: GEMSET.PRG

# libcmini's own start-up, which shrinks the memory and calls main()
GEMSET.PRG: gemset.o
	$(CC) $(LDFLAGS) $(LIBCMINI)/objs/crt0.o $< \
	      -L$(LIBCMINI) -lcmini -lgcc -o $@

gemset.o: gemset.c $(PTOS)/include/machine/rp2350/rp2350_settings.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o GEMSET.PRG
