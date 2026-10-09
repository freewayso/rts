# libpermafrost.so — Android NDK (arm64-v8a)
cmake_minimum_required(VERSION 3.22.1)
project(permafrost_android C ASM)

set(CMAKE_C_STANDARD 99)
add_definitions(
    -D__ANDROID__
    -DPF_GLES
    -DPF_ANDROID_FULL
    -DPF_NO_PYTHON
    -DPF_GLES_WATER_FX=0
)

set(PF_ROOT ${CMAKE_SOURCE_DIR}/..)

add_library(permafrost SHARED
    ${CMAKE_SOURCE_DIR}/../jni/android_bootstrap.c
    ${CMAKE_SOURCE_DIR}/../jni/pf_jni.c
    ${CMAKE_SOURCE_DIR}/../jni/android_engine.c
    ${CMAKE_SOURCE_DIR}/../jni/android_rts_slice.c
    ${CMAKE_SOURCE_DIR}/../jni/android_rts_combat.c
    ${CMAKE_SOURCE_DIR}/../src/render/gles_loader.c
    ${CMAKE_SOURCE_DIR}/../src/sched/context_switch_aarch64.S
)

target_include_directories(permafrost PRIVATE
    ${CMAKE_SOURCE_DIR}/../jni
    ${CMAKE_SOURCE_DIR}/../src
    ${CMAKE_SOURCE_DIR}/../src/render
)

find_library(log-lib log)
find_library(android-lib android)
find_library(GLESv3-lib GLESv3)
find_library(EGL-lib EGL)

target_link_libraries(permafrost
    ${log-lib}
    ${android-lib}
    ${GLESv3-lib}
    ${EGL-lib}
)

# Optional: when SDL2 is built for Android, link it and define PF_ANDROID_SDL
# find_package / add_subdirectory for SDL2 here.
