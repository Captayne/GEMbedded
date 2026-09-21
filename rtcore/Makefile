# Makefile - pTOS real-time core runtime (RP2350, core 1) with IRKernel
#
# Builds rtcore.elf (at the start of flash: it boots and enters pTOS) and, together with the pTOS
# image, one UF2 file carrying both:  make  ->  ptos+rtcore.uf2

PTOS     ?= ../pTOS
IRKERNEL ?= /mnt/d/Develop/_IRF/IRKernel/src

CC       = arm-none-eabi-gcc
OBJDUMP  = arm-none-eabi-objdump

# The kernel runs inside every AES call on core 0, in the middle of a
# program's calculation: its C code must not touch the floating point
# registers, or it corrupts the program's s0..s15 (GCC likes d7 as a
# scratch register for 64-bit constants).  So it is built without the
# FPU.  Only the context switch, which carries s16..s31 across a task
# switch, and the start-up, which enables the unit on core 1, have it.
FPUFLAGS = -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16
CPUFLAGS = -mcpu=cortex-m33 -mthumb -mfloat-abi=soft
CFLAGS   = -O2 -g -Wall -ffreestanding -fno-builtin \
           -ffunction-sections \
           -DIRK_HAVE_LOCAL_CONFIG -I. -I$(IRKERNEL) -iquote $(PTOS)/include
LDFLAGS  = $(FPUFLAGS) -nostartfiles -nostdlib -Wl,-T,rtcore.ld \
           -Wl,--gc-sections -Wl,-Map=rtcore.map

OBJ = crt0.o boot.o kapi.o rtcore.o irk_port_rp2350.o IRKernel.o irk_port_cortexm.o

all: ptos+rtcore.uf2

rtcore.elf: $(OBJ) rtcore.ld
	@$(PTOS)/tools/check-no-fpu.sh $(OBJDUMP) irk_port_cortexm.o $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) -lgcc -o $@

%.o: %.c
	$(CC) $(CPUFLAGS) $(CFLAGS) -c $< -o $@
%.o: %.S
	$(CC) $(FPUFLAGS) $(CFLAGS) -c $< -o $@
IRKernel.o: $(IRKERNEL)/IRKernel.c
	$(CC) $(CPUFLAGS) $(CFLAGS) -c $< -o $@
irk_port_cortexm.o: $(IRKERNEL)/port/irk_port_cortexm.c
	$(CC) $(FPUFLAGS) $(CFLAGS) -c $< -o $@

rtcore.uf2: rtcore.elf
	python3 $(PTOS)/tools/elf2uf2.py $< $@

ptos+rtcore.uf2: rtcore.elf $(PTOS)/emutos.elf
	python3 $(PTOS)/tools/elf2uf2.py $(PTOS)/emutos.elf rtcore.elf $@

clean:
	rm -f *.o *.elf *.map *.uf2

.PHONY: all clean
