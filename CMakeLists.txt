cmake_minimum_required(VERSION 3.10)

project(timesync LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(
    ${PROJECT_NAME}
    src/ptp_control.c
    src/ptp_message.c
)

target_include_directories(${PROJECT_NAME} PUBLIC ./include)
