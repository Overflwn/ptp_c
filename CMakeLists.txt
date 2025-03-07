cmake_minimum_required(VERSION 3.10)

project(timesync LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Used for unit tests
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)

# For Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_library(
    ${PROJECT_NAME}
    src/ptp_control.c
    src/util.c
)

target_include_directories(${PROJECT_NAME} PUBLIC ./include)

if(WIN32)
  target_link_libraries(${PROJECT_NAME} PUBLIC wsock32 ws2_32)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE m)

enable_testing()

add_executable(
    ${PROJECT_NAME}_test
    src/ptp_message_test.cpp
)
target_link_libraries(
    ${PROJECT_NAME}_test
    GTest::gtest_main
    ${PROJECT_NAME}
)

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME}_test)
