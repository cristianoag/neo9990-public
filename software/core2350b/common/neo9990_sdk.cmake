# NEO9990 / Waveshare Core2350B build settings, read by the Pico SDK before project().
#
# The Core2350B is not a stock SDK board, so instead of a board header the
# few board facts the SDK needs are given here, as plain build settings.

set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")
set(PICO_BOARD none CACHE STRING "Board type")

# W25Q128 flash, 16MB (the PSRAM chip select is GP47, see common/board_pins.h)
set(PICO_FLASH_SIZE_BYTES 16777216 CACHE STRING "Flash size in bytes")

add_compile_definitions(
    PICO_RP2350A=0                          # RP2350B: 48 GPIOs
    PICO_FLASH_SIZE_BYTES=16777216
    PICO_BOOT_STAGE2_CHOOSE_W25Q080=1       # second stage for the W25Q128
    PICO_FLASH_SPI_CLKDIV=2
    PICO_RP2350_A2_SUPPORTED=1
)

if(NOT PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
if(NOT PICO_SDK_PATH OR NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
    message(FATAL_ERROR "Pico SDK not found: pass -DPICO_SDK_PATH=<sdk> or set the PICO_SDK_PATH environment variable")
endif()
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
