
# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.
cmake_minimum_required(VERSION 3.18.1)

project("kernelsu")

find_package(cxx REQUIRED CONFIG)
link_libraries(cxx::cxx)

add_library(kernelsu
        SHARED
        jni.cc
        ksu.cc
        )

# MRSU: cpp/uapi 是本地 junction，git 不跟踪；CI 中必须回指仓库根 uapi 才能找到 uapi/ksu.h
target_include_directories(kernelsu PRIVATE . ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../uapi)

find_library(log-lib log)

target_link_libraries(kernelsu ${log-lib})

add_library(adbroot SHARED adbroot.cc)
target_link_libraries(adbroot ${log-lib})
