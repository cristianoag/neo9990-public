# Shared build target of the NEO9990 Core2350B firmwares.
#
# The V9990 and V9968 CMakeLists.txt set NEO_CHIP, include neo9990_sdk.cmake
# (SDK setup), call project() and pico_sdk_init(), then include this file.

set(NEO_COMMON ${CMAKE_CURRENT_LIST_DIR})
set(NEO_CORE   ${CMAKE_CURRENT_LIST_DIR}/../core)

string(TOLOWER ${NEO_CHIP} NEO_CHIP_LOWER)
set(NEO_TARGET neo9990_${NEO_CHIP_LOWER})

set(NEO_FW_VERSION "v1.00" CACHE STRING "Firmware version")

set(NEO_SOURCES
    ${NEO_COMMON}/bus.c
    ${NEO_COMMON}/dvi_out.c
    ${NEO_COMMON}/platform.c
)

if(NEO_CHIP STREQUAL "V9990")
    list(APPEND NEO_SOURCES
        ${NEO_COMMON}/psram.c
        ${NEO_CORE}/v9990.c
        ${NEO_CORE}/v9990_cmd.c
        ${NEO_CORE}/v9990_render.c)
elseif(NEO_CHIP STREQUAL "V9968")
    list(APPEND NEO_SOURCES
        ${NEO_CORE}/v9968.c
        ${NEO_CORE}/v9968_cmd.c
        ${NEO_CORE}/v9968_render.c
        ${NEO_CORE}/geo3d.c)
else()
    message(FATAL_ERROR "NEO_CHIP must be V9990 or V9968")
endif()

add_executable(${NEO_TARGET} ${CMAKE_CURRENT_LIST_DIR}/../${NEO_CHIP_LOWER}/src/main.c ${NEO_SOURCES})

pico_generate_pio_header(${NEO_TARGET} ${NEO_COMMON}/bus.pio)

target_include_directories(${NEO_TARGET} PRIVATE ${NEO_COMMON} ${NEO_CORE})
target_compile_options(${NEO_TARGET} PRIVATE -Wall -Wextra -Wno-unused-parameter -O2)
target_compile_definitions(${NEO_TARGET} PRIVATE
    NEO_CHIP_${NEO_CHIP}=1
    NEO_FW_VERSION="${NEO_FW_VERSION}")

if(NEO_CHIP STREQUAL "V9968")
    # renderer stage timing for "stat"
    target_compile_definitions(${NEO_TARGET} PRIVATE V9968_PROF=1)
endif()

target_link_libraries(${NEO_TARGET}
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_dma
    hardware_clocks
    hardware_pll
    hardware_vreg
)

# Console over USB CDC (the UART pins belong to the MSX bus).
pico_enable_stdio_usb(${NEO_TARGET} 1)
pico_enable_stdio_uart(${NEO_TARGET} 0)

# Execute from SRAM. Instruction fetches from flash stall whenever the QMI is
# busy with the PSRAM, and small helpers (memcpy, the blitters, geo3d) are easy
# to end up in flash by accident. Flash is read once, at boot.
pico_set_binary_type(${NEO_TARGET} copy_to_ram)

pico_add_extra_outputs(${NEO_TARGET})
