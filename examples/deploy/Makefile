# Makefile - DEPLOY.PRG, receives programs over the USB console
#
# Uses the libcmini that the pTOS build produces.  DEPLOY.PRG belongs on
# the flash drive (F:) or the SD card; it must be running for
# tools/ptosdeploy.py to be able to send anything.

PTOS     ?= ../../pTOS
LIBCMINI  = $(PTOS)/obj/libcmini-build
LIB       = ../../apps/lib

CC       = arm-none-eabi-gcc
CPUFLAGS = -mcpu=cortex-m33 -mthumb -mfloat-abi=soft
CFLAGS   = $(CPUFLAGS) -O2 -g -Wall -mword-relocations \
           -I$(PTOS)/lib/libcmini/include -iquote $(PTOS)/include -iquote $(LIB)
LDFLAGS  = $(CPUFLAGS) -nostartfiles -nostdlib \
           -Wl,-q -Wl,-Ttext=0 -Wl,-e_start

OBJS = accstart.o deploy.o gem.o

all: DEPLOY.PRG

DEPLOY.PRG: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -L$(LIBCMINI) -lcmini -lgcc -o $@

accstart.o: $(LIB)/accstart.S
	$(CC) $(CPUFLAGS) -c $< -o $@

gem.o: $(LIB)/gem.c $(LIB)/gem.h
	$(CC) $(CFLAGS) -c $< -o $@

deploy.o: deploy.c $(PTOS)/include/usbcon.h $(LIB)/gem.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o DEPLOY.PRG

.PHONY: all clean
