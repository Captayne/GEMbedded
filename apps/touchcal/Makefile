# Makefile - touch screen calibration (desk accessory TOUCHCAL.ACC)
#
# Uses the libcmini that the pTOS build produces.  TOUCHCAL.ACC goes into
# the root directory of the boot drive (C:) on the SD card; renamed to
# TOUCHCAL.PRG it runs as a program too.

PTOS     ?= ../../pTOS
LIBCMINI  = $(PTOS)/obj/libcmini-build
LIB       = ../lib

CC       = arm-none-eabi-gcc
CPUFLAGS = -mcpu=cortex-m33 -mthumb -mfloat-abi=soft
CFLAGS   = $(CPUFLAGS) -O2 -g -Wall -mword-relocations \
           -I$(PTOS)/lib/libcmini/include -iquote $(PTOS)/include -iquote $(LIB)
LDFLAGS  = $(CPUFLAGS) -nostartfiles -nostdlib \
           -Wl,-q -Wl,-Ttext=0 -Wl,-e_start

OBJS = accstart.o touchcal.o gem.o

all: TOUCHCAL.ACC

TOUCHCAL.ACC: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -L$(LIBCMINI) -lcmini -lgcc -o $@

accstart.o: $(LIB)/accstart.S
	$(CC) $(CPUFLAGS) -c $< -o $@

gem.o: $(LIB)/gem.c $(LIB)/gem.h
	$(CC) $(CFLAGS) -c $< -o $@

touchcal.o: touchcal.c $(PTOS)/include/touch.h $(LIB)/gem.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o TOUCHCAL.ACC

.PHONY: all clean
