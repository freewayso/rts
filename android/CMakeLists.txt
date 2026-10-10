# libpermafrost.so — Android NDK (arm64-v8a)
#
# Default: JNI + GLES stub triangle (builds without full engine).
# Set -DPF_ANDROID_LINK_ENGINE=ON to pull src/** (no Python) into the .so.
# See android/README.md "Current status" and docs/android_apk_so.md.
cmake_minimum_required(VERSION 3.22.1)
project(permafrost_android C ASM)

set(CMAKE_C_STANDARD 99)

option(PF_ANDROID_LINK_ENGINE "Link engine src into libpermafrost.so (no Python)" OFF)

add_definitions(
    -D__ANDROID__
    -DPF_GLES
    -DPF_ANDROID_FULL
    -DPF_NO_PYTHON
    -DPF_NO_MIMALLOC
    -DPF_GLES_WATER_FX=0
)

set(PF_ROOT ${CMAKE_SOURCE_DIR}/..)

set(PF_ANDROID_JNI_SRCS
    ${PF_ROOT}/jni/android_bootstrap.c
    ${PF_ROOT}/jni/pf_jni.c
    ${PF_ROOT}/jni/android_engine.c
    ${PF_ROOT}/jni/android_rts_slice.c
    ${PF_ROOT}/jni/android_rts_combat.c
    ${PF_ROOT}/jni/android_mp.c
    ${PF_ROOT}/src/render/gles_loader.c
)

if(PF_ANDROID_LINK_ENGINE)
    # OpenAL Soft not yet built for Android — stub audio symbols.
    list(APPEND PF_ANDROID_JNI_SRCS ${PF_ROOT}/jni/android_audio_stub.c)
    # Sync sched stub until aarch64 fiber sched.c is ready
    list(APPEND PF_ANDROID_JNI_SRCS ${PF_ROOT}/jni/android_sched_stub.c)
    # Desktop GL perf queries → no-ops on ES3
    list(APPEND PF_ANDROID_JNI_SRCS ${PF_ROOT}/jni/android_gl_perf_stub.c)
    # main.c / script / perf globals & entry points
    list(APPEND PF_ANDROID_JNI_SRCS ${PF_ROOT}/jni/android_engine_link_stubs.c)
endif()

# Fiber asm only when engine/sched is linked (needs sched_task_exit).
set(PF_ANDROID_ASM_SRCS "")

if(PF_ANDROID_LINK_ENGINE)
    include(${CMAKE_SOURCE_DIR}/cmake/EngineSources.cmake)
    pf_android_collect_engine_sources(PF_ENGINE_SRCS)
    add_definitions(-DPF_ANDROID_ENGINE_LINKED=1)
    if(PF_ANDROID_ENGINE_BATCH GREATER_EQUAL 2)
        add_definitions(-DPF_ANDROID_HAVE_NET=1)
        message(STATUS "PF_ANDROID_HAVE_NET=1 (batch ${PF_ANDROID_ENGINE_BATCH})")
    endif()
    message(STATUS "PF_ANDROID_LINK_ENGINE=ON  sources=${PF_ENGINE_SRCS}")
else()
    set(PF_ENGINE_SRCS "")
    message(STATUS "PF_ANDROID_LINK_ENGINE=OFF (stub .so)")
endif()

add_library(permafrost SHARED
    ${PF_ANDROID_JNI_SRCS}
    ${PF_ENGINE_SRCS}
    ${PF_ANDROID_ASM_SRCS}
)

target_include_directories(permafrost PRIVATE
    # Fake <GL/glew.h> → GLES3 (must be first)
    ${PF_ROOT}/jni/android_gl_headers
    # Stub mimalloc headers until NDK mimalloc is linked
    ${PF_ROOT}/jni/android_mimalloc_headers
    ${PF_ROOT}/jni
    ${PF_ROOT}/src
    ${PF_ROOT}/src/render
    ${PF_ROOT}/src/lib
    ${PF_ROOT}/src/lib/public
    ${PF_ROOT}/src/game/public
    ${PF_ROOT}/src/map/public
    ${PF_ROOT}/src/render/public
    ${PF_ROOT}/src/sched/public
    ${PF_ROOT}/src/event/public
    ${PF_ROOT}/src/entity/public
    ${PF_ROOT}/src/navigation/public
    ${PF_ROOT}/src/audio/public
    ${PF_ROOT}/src/phys/public
    ${PF_ROOT}/src/animation/public
    ${PF_ROOT}/src/ui/public
    ${PF_ROOT}/src/cursor/public
    ${PF_ROOT}/src/asset_load/public
    ${PF_ROOT}/src/settings
    ${PF_ROOT}/src/net/public
    ${PF_ROOT}/src/lockstep/public
)

find_library(log-lib log)
find_library(android-lib android)
find_library(GLESv3-lib GLESv3)
find_library(EGL-lib EGL)
find_library(m-lib m)

target_link_libraries(permafrost
    ${log-lib}
    ${android-lib}
    ${GLESv3-lib}
    ${EGL-lib}
    ${m-lib}
)

if(PF_ANDROID_LINK_ENGINE)
    # Build SDL2 for Android from deps (provides headers + lib).
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
    add_subdirectory(${PF_ROOT}/deps/SDL2 ${CMAKE_BINARY_DIR}/sdl2-build EXCLUDE_FROM_ALL)
    target_link_libraries(permafrost SDL2)
    target_include_directories(permafrost PRIVATE ${PF_ROOT}/deps/SDL2/include)
endif()
