# Makefile - INETFETCH.PRG, a URL in and the reply out
#
# The same arrangement as ../Fractals: libcmini from the pTOS build, the
# board's own AES/VDI bindings, and the sketch compiled as C++ because
# make knows nothing about a .ino.
#
# This one also needs the GEMduino library -- EspAt for the module and
# Uart for the port it hangs on.

PTOS     ?= ../../../pTOS
LIBCMINI  = $(PTOS)/obj/libcmini-build
LIB       = ../hardware/gembedded/rp2350/cores/gem
GEMDUINO  = ../hardware/gembedded/rp2350/libraries/GEMduino/src

CC       = arm-none-eabi-gcc
CXX      = arm-none-eabi-g++
CPUFLAGS = -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16
CFLAGS   = $(CPUFLAGS) -O2 -g -Wall -mword-relocations \
           -I$(LIB)/compat \
           -I$(GEMDUINO) \
           -I$(PTOS)/lib/libcmini/include \
           -I$(PTOS)/lib/libcmini/include/mint \
           -iquote $(PTOS)/include \
           -iquote $(LIB) -I$(LIB)
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics
LDFLAGS  = $(CPUFLAGS) -nostartfiles -nostdlib \
           -Wl,-q -Wl,-Ttext=0 -Wl,-e_start

OBJS = inetfetch.o espat.o uart.o gemduino.o gem.o

all: INETFETCH.PRG

INETFETCH.PRG: $(OBJS)
	$(CC) $(LDFLAGS) $(LIBCMINI)/objs/minicrt0.o $(OBJS) \
	    -L$(LIBCMINI) -lcmini -lgcc -o $@

inetfetch.o: InetFetch.ino $(GEMDUINO)/EspAt.h $(LIB)/gem.h
	$(CXX) $(CXXFLAGS) -x c++ -c $< -o $@

espat.o: $(GEMDUINO)/EspAt.cpp $(GEMDUINO)/EspAt.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

uart.o: $(GEMDUINO)/Uart.cpp $(GEMDUINO)/Uart.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

gemduino.o: $(GEMDUINO)/GEMduino.cpp $(GEMDUINO)/GEMduino.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

gem.o: $(LIB)/gem.c $(LIB)/gem.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o INETFETCH.PRG

.PHONY: all clean
