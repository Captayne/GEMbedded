/*
 * Board definition for the Waveshare RP2350-PiZero (RP2350B, 48 GPIO).
 * Pin assignments as documented by the XRoar RP2350-PiZero port, which
 * states they were checked against the Waveshare schematic.
 */
#ifndef _BOARDS_WAVESHARE_RP2350_PIZERO_H
#define _BOARDS_WAVESHARE_RP2350_PIZERO_H

pico_board_cmake_set(PICO_PLATFORM, rp2350-arm-s)

#define WAVESHARE_RP2350_PIZERO

/* RP2350B: 48 GPIOs */
#define PICO_RP2350A 0

#define PICO_DEFAULT_UART 0
#define PICO_DEFAULT_UART_TX_PIN 0
#define PICO_DEFAULT_UART_RX_PIN 1

#define PICO_DEFAULT_WS2812_PIN 2

#define PIZERO_SD_SCK_PIN   30
#define PIZERO_SD_MOSI_PIN  31
#define PIZERO_SD_MISO_PIN  40
#define PIZERO_SD_CS_PIN    43
#define PIZERO_SD_CD_PIN    22

#define PIZERO_PSRAM_CS_PIN 47

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#define PICO_FLASH_SPI_CLKDIV 2
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)

#endif
