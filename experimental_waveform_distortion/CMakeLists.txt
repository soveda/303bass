cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "/Users/adrianvos/pico-sdk" CACHE PATH "Raspberry Pi Pico SDK")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(Fr330hfr33Experimental C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(Fr330hfr33Experimental
    Fr330hfr33.cpp
    Fr330hfr33_LUT.cpp
    usb_descriptors.c
    usb_midi_host.c
    usb_midi_host_app_driver.c
)

target_include_directories(Fr330hfr33Experimental PRIVATE
    .
)

target_link_libraries(Fr330hfr33Experimental
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

target_compile_definitions(Fr330hfr33Experimental PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    FR330HFR33_OVERCLOCK_KHZ=192000
)

pico_enable_stdio_usb(Fr330hfr33Experimental 0)
pico_set_binary_type(Fr330hfr33Experimental copy_to_ram)

target_compile_options(Fr330hfr33Experimental PRIVATE
    -O3
    -Wdouble-promotion
    -Wfloat-conversion
    -Wall
    -Wextra
)

target_link_options(Fr330hfr33Experimental PRIVATE
    -Wl,--print-memory-usage
)

pico_add_extra_outputs(Fr330hfr33Experimental)
