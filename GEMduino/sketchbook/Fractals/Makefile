# Makefile - FRACTALS.PRG, three computations sharing the real-time core
#
# Uses the libcmini that the pTOS build produces ("make test-hd" in pTOS
# builds it once) and the AES/VDI bindings of the board's own core.

PTOS     ?= ../../../pTOS
LIBCMINI  = $(PTOS)/obj/libcmini-build
LIB       = ../hardware/gembedded/rp2350/cores/gem

CC       = arm-none-eabi-gcc
CXX      = arm-none-eabi-g++
CPUFLAGS = -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16
# $(LIB)/compat comes first, exactly as platform.txt places it for
# the IDE: a C++ translation unit that includes <math.h> reaches
# newlib's stdlib.h behind libcmini's back, and the two disagree
# about what sys/cdefs.h holds.  The file there explains it.
CFLAGS   = $(CPUFLAGS) -O2 -g -Wall -Wextra -mword-relocations \
           -I$(LIB)/compat \
           -I$(PTOS)/lib/libcmini/include -iquote $(PTOS)/include \
           -iquote $(LIB)
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics
LDFLAGS  = $(CPUFLAGS) -nostartfiles -nostdlib \
           -Wl,-q -Wl,-Ttext=0 -Wl,-e_start

# The program lives in Fractals.ino now, not in a .c file: the sketch
# was converted to the single-file C++ form the board package supports.
# Make has to be told, because a .ino means nothing to it -- hence the
# C++ compiler and -x c++ for this one object.
OBJS = fractals.o gem.o mathglue.o

all: FRACTALS.PRG

FRACTALS.PRG: $(OBJS)
	$(CC) $(LDFLAGS) $(LIBCMINI)/objs/minicrt0.o $(OBJS) \
	    -L$(LIBCMINI) -lcmini -lm -lgcc -o $@

fractals.o: Fractals.ino $(PTOS)/include/irk.h $(LIB)/gem.h $(LIB)/mathglue.h
	$(CXX) $(CXXFLAGS) -x c++ -c $< -o $@

gem.o: $(LIB)/gem.c $(LIB)/gem.h
	$(CC) $(CFLAGS) -c $< -o $@

mathglue.o: $(LIB)/mathglue.c $(LIB)/mathglue.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o FRACTALS.PRG

.PHONY: all clean
