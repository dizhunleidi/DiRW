cmake_minimum_required(VERSION 3.10)


# 如果未设置 NDK 路径，尝试从环境变量获取
if(NOT DEFINED CMAKE_ANDROID_NDK)
    set(CMAKE_ANDROID_NDK $ENV{NDK_HOME})
endif()
if(CMAKE_ANDROID_NDK AND NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake)
endif()

# Android NDK 设置
set(CMAKE_SYSTEM_NAME Android)
set(CMAKE_SYSTEM_VERSION 28)
set(ANDROID_ABI arm64-v8a)
set(ANDROID_PLATFORM android-26)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output)

project(DiRW_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/diRW
)

set(SOURCES
    example.cpp
)



add_executable(DiRW_test ${SOURCES})

target_link_libraries(DiRW_test log)
