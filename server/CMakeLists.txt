cmake_minimum_required(VERSION 3.16)
project(permafrost_server C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

if(WIN32)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  set(PF_NET_LIBS ws2_32 crypt32 advapi32)
else()
  set(PF_NET_LIBS pthread)
endif()

find_package(Protobuf REQUIRED)

set(PF_COMMON
  common/pf_crypto.cpp
  common/tcp_net.cpp
  common/pb_frame.cpp
  common/pf_log.cpp
)
set(PF_PB_GEN
  proto/gen/pf_common.pb.cc
  proto/gen/pf_login.pb.cc
  proto/gen/pf_room.pb.cc
)
set(PF_STORAGE storage/storage_service.cpp)

set(PF_SQLITE_DIR "${CMAKE_SOURCE_DIR}/third_party/sqlite/sqlite-amalgamation-3460100")
set(PF_HAS_SQLITE_SRC FALSE)
if(EXISTS "${PF_SQLITE_DIR}/sqlite3.c")
  set(PF_HAS_SQLITE_SRC TRUE)
  message(STATUS "SQLite amalgamation found — enabling PF_HAS_SQLITE")
endif()

add_executable(login_server
  login/login_main.cpp
  ${PF_COMMON}
  ${PF_PB_GEN}
  ${PF_STORAGE}
)
target_include_directories(login_server PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/proto/gen
  ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(login_server PRIVATE ${PF_NET_LIBS} protobuf::libprotobuf)

if(PF_HAS_SQLITE_SRC)
  target_compile_definitions(login_server PRIVATE PF_HAS_SQLITE)
  target_sources(login_server PRIVATE "${PF_SQLITE_DIR}/sqlite3.c")
  target_include_directories(login_server PRIVATE "${PF_SQLITE_DIR}")
endif()

add_executable(room_server
  room/room_main.cpp
  ${PF_COMMON}
  ${PF_PB_GEN}
)
target_include_directories(room_server PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/proto/gen
  ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(room_server PRIVATE ${PF_NET_LIBS} protobuf::libprotobuf)

# Windows GUI client (login + room list/create/join/delete)
add_executable(online_client_ui WIN32
  client_ui/main_win.cpp
  client_ui/map_preview.cpp
  client_ui/engine_launch.cpp
  ${CMAKE_SOURCE_DIR}/../src/net/online_client.cpp
  ${PF_COMMON}
  ${PF_PB_GEN}
)
target_include_directories(online_client_ui PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/proto/gen
  ${CMAKE_SOURCE_DIR}/../src
  ${CMAKE_SOURCE_DIR}/../src/net
  ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(online_client_ui PRIVATE
  ${PF_NET_LIBS} protobuf::libprotobuf comctl32 user32 gdi32
)

# Console smoke test (register/login/room/match)
add_executable(online_smoke_test
  client_ui/smoke_test.cpp
  ${CMAKE_SOURCE_DIR}/../src/net/online_client.cpp
  ${PF_COMMON}
  ${PF_PB_GEN}
)
target_include_directories(online_smoke_test PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/proto/gen
  ${CMAKE_SOURCE_DIR}/../src
  ${CMAKE_SOURCE_DIR}/../src/net
  ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(online_smoke_test PRIVATE ${PF_NET_LIBS} protobuf::libprotobuf)

add_executable(join_smoke
  client_ui/join_smoke.cpp
  ${CMAKE_SOURCE_DIR}/../src/net/online_client.cpp
  ${PF_COMMON}
  ${PF_PB_GEN}
)
target_include_directories(join_smoke PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/proto/gen
  ${CMAKE_SOURCE_DIR}/../src
  ${CMAKE_SOURCE_DIR}/../src/net
  ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(join_smoke PRIVATE ${PF_NET_LIBS} protobuf::libprotobuf)

# On Windows/MinGW: copy runtime DLLs next to exes after build (libstdc++/protobuf/absl).
if(WIN32)
  add_custom_target(deploy_runtime_dlls
    COMMAND ${CMAKE_COMMAND} -E echo "Deploying MinGW runtime DLLs..."
    COMMAND "$ENV{SystemRoot}/System32/WindowsPowerShell/v1.0/powershell.exe"
            -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/deploy_runtime_dlls.ps1"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Copy MinGW/protobuf DLLs into build/"
    VERBATIM
  )
  add_dependencies(login_server deploy_runtime_dlls)
  add_dependencies(room_server deploy_runtime_dlls)
  add_dependencies(online_client_ui deploy_runtime_dlls)
endif()

# Regenerate: protoc -I proto --cpp_out=proto/gen proto/pf_*.proto
