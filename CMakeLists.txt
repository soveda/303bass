cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "/Users/adrianvos/pico-sdk" CACHE PATH "Raspberry Pi Pico SDK")
set(CIZZLE_PATH "/Users/adrianvos/Documents/GitHub/Cizzle" CACHE PATH "Cizzle reference firmware")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(Fr330hfr33 C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(Fr330hfr33
    Fr330hfr33.cpp
    Fr330hfr33_LUT.cpp
    usb_descriptors.c
    ${CIZZLE_PATH}/usb_midi_host.c
    ${CIZZLE_PATH}/usb_midi_host_app_driver.c
)

target_include_directories(Fr330hfr33 PRIVATE
    .
    ${CIZZLE_PATH}
)

target_link_libraries(Fr330hfr33
    pico_stdlib
    pico_unique_id
    pico_multicore
    tinyusb_device
    tinyusb_host
    tinyusb_board
    hardware_adc
    hardware_pwm
    hardware_dma
    hardware_spi
    hardware_clocks
    hardware_i2c
)

target_compile_definitions(Fr330hfr33 PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    FR330HFR33_OVERCLOCK_KHZ=192000
)

pico_enable_stdio_usb(Fr330hfr33 0)
pico_set_binary_type(Fr330hfr33 copy_to_ram)

target_compile_options(Fr330hfr33 PRIVATE
    -O3
    -Wdouble-promotion
    -Wfloat-conversion
    -Wall
    -Wextra
)

target_link_options(Fr330hfr33 PRIVATE
    -Wl,--print-memory-usage
)

pico_add_extra_outputs(Fr330hfr33)
