cmake_minimum_required(VERSION 3.13)

set(NEO_CHIP V9990)
include(${CMAKE_CURRENT_LIST_DIR}/../common/neo9990_sdk.cmake)

project(neo9990_v9990 C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

include(${CMAKE_CURRENT_LIST_DIR}/../common/neo9990.cmake)
