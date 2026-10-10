# Engine sources for libpermafrost.so (A3.4 progressive link).
# PF_ANDROID_LINK_ENGINE=ON enables batches below.
#
# Batch 0 (default OFF path in CMakeLists): JNI stub only.
# Batch 1+: add src trees excluding Python scripting.

# Batch 2+ includes src/net (UDP lobby). Default 2 for LAN multiplayer APK builds.
set(PF_ANDROID_ENGINE_BATCH "2" CACHE STRING "Engine link batch: 1=core+game+lockstep; 2+=net/lobby")

set(_PF_ROOT "${CMAKE_SOURCE_DIR}/..")

function(pf_android_collect_engine_sources out_var)
  set(_srcs "")

  # --- always exclude desktop entry / python ---
  set(_exclude_names
    main.c
  )

  # Core / platform-ish (audio deferred — needs OpenAL Soft Android build)
  file(GLOB _batch_core
    "${_PF_ROOT}/src/*.c"
    "${_PF_ROOT}/src/lib/*.c"
    "${_PF_ROOT}/src/event/*.c"
    "${_PF_ROOT}/src/entity/*.c"
    "${_PF_ROOT}/src/ui/*.c"
    "${_PF_ROOT}/src/cursor/*.c"
    "${_PF_ROOT}/src/settings/*.c"
    "${_PF_ROOT}/src/asset_load/*.c"
    "${_PF_ROOT}/src/anim/*.c"
    "${_PF_ROOT}/src/phys/*.c"
    "${_PF_ROOT}/src/navigation/*.c"
  )

  # Render (GLES path)
  file(GLOB _batch_render "${_PF_ROOT}/src/render/*.c")

  # Map + game
  file(GLOB _batch_map "${_PF_ROOT}/src/map/*.c")
  file(GLOB _batch_game "${_PF_ROOT}/src/game/*.c")

  # Optional net/lockstep (may pull extra deps)
  file(GLOB _batch_net "${_PF_ROOT}/src/net/*.c")
  file(GLOB _batch_ls "${_PF_ROOT}/src/lockstep/*.c")

  # Explicitly skip Python scripting tree (src/script/*.c not added)
  # Skip desktop-only / heavy deps in batch 1
  list(APPEND _exclude_names
    al_audio.c
    al_effect.c
    # nk_file_browser.c kept out — stub nk_file_list in link stubs
    nk_file_browser.c
    perf.c
    sched.c
    # Desktop GL query / NVX / pipeline stats — replaced by jni stub
    gl_perf.c
    # Already provided by android CMake JNI list
    gles_loader.c
  )

  # Core + render + map + game + lockstep (needed by game command paths)
  set(_all ${_batch_core} ${_batch_render} ${_batch_map} ${_batch_game} ${_batch_ls})
  if(PF_ANDROID_ENGINE_BATCH GREATER_EQUAL 2)
    list(APPEND _all ${_batch_net})
  endif()

  foreach(f ${_all})
    get_filename_component(bn "${f}" NAME)
    get_filename_component(bdir "${f}" DIRECTORY)
    get_filename_component(bdirname "${bdir}" NAME)
    # Skip audio directory entirely in batch 1
    if(bdirname STREQUAL "audio")
      continue()
    endif()
    list(FIND _exclude_names "${bn}" _idx)
    if(_idx EQUAL -1)
      list(APPEND _srcs "${f}")
    endif()
  endforeach()

  # Fiber asm deferred until full aarch64 sched.c is enabled
  set(${out_var} "${_srcs}" PARENT_SCOPE)
endfunction()
