cmake_minimum_required(VERSION 3.13)

#Defualt to pico_w
set(PICO_BOARD pico_w)

if(NOT DEFINED PICO_BOARD)
    message(FATAL_ERROR "This program is for Pico W board, please define PICO_BOARD to pico_w")
endif()

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

set(BLUEPAD32_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/bluepad32)

# To use BTstack from Pico SDK do
#set(BTSTACK_ROOT ${PICO_SDK_PATH}/lib/btstack)
# To use it from Bluepad32 (up-to-date, with custom patches for controllers):
set(BTSTACK_ROOT ${BLUEPAD32_ROOT}/external/btstack)

project(SwitchKMAdapter C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

file(GLOB_RECURSE SOURCES "src/*.c")
add_executable(SwitchKMAdapter ${SOURCES})

target_include_directories(SwitchKMAdapter PRIVATE
    src
    bluepad32/src/components/bluepad32/include)
include_directories(include)

# Needed for btstack_config.h / sdkconfig.h
# so that libblupad32 can include them
include_directories(SwitchKMAdapter src)

target_link_libraries(SwitchKMAdapter
    pico_stdlib
    pico_cyw43_arch_none
    pico_btstack_classic
    pico_btstack_cyw43
    bluepad32
    tinyusb_device
    tinyusb_board
    pico_multicore
)

add_subdirectory(bluepad32/src/components/bluepad32 libbluepad32)

pico_enable_stdio_usb(SwitchKMAdapter 0)
pico_enable_stdio_uart(SwitchKMAdapter 0)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(SwitchKMAdapter)
