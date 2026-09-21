# Makefile - pTOS real-time core runtime (RP2350, core 1) with IRKernel
#
# Builds rtcore.elf (at the start of flash: it boots and enters pTOS) and, together with the pTOS
# image, one UF2 file carrying both:  make  ->  ptos+rtcore.uf2

PTOS     ?= ../pTOS
IRKERNEL ?= /mnt/d/Develop/_IRF/IRKernel/src

CC       = arm-none-eabi-gcc
CPUFLAGS = -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16
CFLAGS   = $(CPUFLAGS) -O2 -g -Wall -ffreestanding -fno-builtin \
           -ffunction-sections \
           -DIRK_HAVE_LOCAL_CONFIG -I. -I$(IRKERNEL) -iquote $(PTOS)/include
LDFLAGS  = $(CPUFLAGS) -nostartfiles -nostdlib -Wl,-T,rtcore.ld \
           -Wl,--gc-sections -Wl,-Map=rtcore.map

OBJ = crt0.o boot.o kapi.o rtcore.o irk_port_rp2350.o IRKernel.o irk_port_cortexm.o

all: ptos+rtcore.uf2

rtcore.elf: $(OBJ) rtcore.ld
	$(CC) $(LDFLAGS) $(OBJ) -lgcc -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@
IRKernel.o: $(IRKERNEL)/IRKernel.c
	$(CC) $(CFLAGS) -c $< -o $@
irk_port_cortexm.o: $(IRKERNEL)/port/irk_port_cortexm.c
	$(CC) $(CFLAGS) -c $< -o $@

rtcore.uf2: rtcore.elf
	python3 $(PTOS)/tools/elf2uf2.py $< $@

ptos+rtcore.uf2: rtcore.elf $(PTOS)/emutos.elf
	python3 $(PTOS)/tools/elf2uf2.py $(PTOS)/emutos.elf rtcore.elf $@

clean:
	rm -f *.o *.elf *.map *.uf2

.PHONY: all clean
