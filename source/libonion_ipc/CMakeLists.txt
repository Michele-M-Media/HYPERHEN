###################################################################################################
# HyperHEN shared IPC client (injectee-side: shellui)
###################################################################################################

cmake_minimum_required(VERSION 3.20)

set(basename "onion_ipc")
project(${basename} C CXX)

onion_module_prologue(C_STANDARD 11 CXX_STANDARD 20)

set(D_CWD "${PROJECT_SOURCE_DIR}")

add_library(${PROJECT_NAME} STATIC
  source/ipc_client.cpp
  source/ipc_server.cpp
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -fstack-protector-all")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wextra -Werror -fno-exceptions")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g0 -O2")

target_include_directories(${PROJECT_NAME}
  PUBLIC  "${D_CWD}/include"
  PRIVATE "${D_CWD}/../include"
  PUBLIC  "${D_CWD}/../libonion_platform/include"
  PRIVATE "${HYPERHEN_THIRD_PARTY_DIR}/cjson"
)

# cJSON symbols are provided by the final link unit (shellui already
# compile third_party/cjson into the ELF). Keep them unresolved here.

