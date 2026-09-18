cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD waveshare_rp2350_pizero)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(board_test C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(board_test main.c)
pico_generate_pio_header(board_test ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)
target_link_libraries(board_test pico_stdlib pico_unique_id hardware_pio)

pico_enable_stdio_usb(board_test 1)
pico_enable_stdio_uart(board_test 1)

pico_add_extra_outputs(board_test)
