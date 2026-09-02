# Template: cmake-sdk
# Goal: CMake project linking against full RK SDK sysroot (rknpu2, mpp, rga,
#       rockit, etc). Use for complex multi-module apps that need header search
#       paths and transitive lib deps resolved by the SDK.
#
# Usage:
#   mkdir build && cd build
#   cmake -DCMAKE_TOOLCHAIN_FILE=<toolchain.cmake> \
#         -DRK_SDK_PATH=<path-to-rk-sdk-sysroot> ..
#   make -j8
#
# RK_SDK_PATH layout (see references/sdk-sysroot.md):
#   <RK_SDK_PATH>/
#     include/        (rk_mpi.h, rknn_api.h, im2d.h, ...)
#     lib/            (librockchip_mpp.so, librknnrt.so, librga.so, ...)
#     lib/aarch64/    or lib/armhf for RV1106
#
# Pitfalls:
#  - RV1106: set ARCH=armhf and link librknnmrt (note 'm' = micro).
#  - glibc 2.31 boards (RK3568): do NOT link host .so directly, use board's
#    libc.so.6 for crt. See knowledge/cross-compile-glibc.md.
cmake_minimum_required(VERSION 3.16)
project(rk_sdk_app C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

# --- RK SDK paths (set via -DRK_SDK_PATH=...) ---
if(NOT DEFINED RK_SDK_PATH)
    message(FATAL_ERROR "RK_SDK_PATH not set. Point to RK SDK sysroot.")
endif()
if(NOT DEFINED RK_ARCH)
    set(RK_ARCH "aarch64")  # or "armhf" for RV1106
endif()

include_directories(${RK_SDK_PATH}/include)
link_directories(${RK_SDK_PATH}/lib/${RK_ARCH})

# --- RK libraries ---
set(RK_LIBS
    rockchip_mpp
    rockchip_mpp_v2
    rga
    rknnrt
)
if(RK_ARCH STREQUAL "armhf")
    list(REMOVE_ITEM RK_LIBS rknnrt)
    list(APPEND RK_LIBS rknnmrt)  # RV1106 micro runtime
endif()

add_executable(rk_sdk_app main.cpp)
target_link_libraries(rk_sdk_app ${RK_LIBS} pthread)

# Install to bin/ for packaging
install(TARGETS rk_sdk_app RUNTIME DESTINATION bin)