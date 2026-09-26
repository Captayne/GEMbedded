# Makefile - FRACTALS.PRG, three computations sharing the real-time core
#
# Uses the libcmini that the pTOS build produces ("make test-hd" in pTOS
# builds it once) and the AES/VDI bindings of the board's own core.

PTOS     ?= ../../../pTOS
LIBCMINI  = $(PTOS)/obj/libcmini-build
LIB       = ../hardware/gembedded/rp2350/cores/gem

CC       = arm-none-eabi-gcc
CPUFLAGS = -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16
CFLAGS   = $(CPUFLAGS) -O2 -g -Wall -Wextra -mword-relocations \
           -I$(PTOS)/lib/libcmini/include -iquote $(PTOS)/include \
           -iquote $(LIB)
LDFLAGS  = $(CPUFLAGS) -nostartfiles -nostdlib \
           -Wl,-q -Wl,-Ttext=0 -Wl,-e_start

OBJS = fractals.o gem.o mathglue.o

all: FRACTALS.PRG

FRACTALS.PRG: $(OBJS)
	$(CC) $(LDFLAGS) $(LIBCMINI)/objs/minicrt0.o $(OBJS) \
	    -L$(LIBCMINI) -lcmini -lm -lgcc -o $@

fractals.o: fractals.c $(PTOS)/include/irk.h $(LIB)/gem.h $(LIB)/mathglue.h
	$(CC) $(CFLAGS) -c $< -o $@

gem.o: $(LIB)/gem.c $(LIB)/gem.h
	$(CC) $(CFLAGS) -c $< -o $@

mathglue.o: $(LIB)/mathglue.c $(LIB)/mathglue.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o FRACTALS.PRG

.PHONY: all clean
