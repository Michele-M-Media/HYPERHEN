###################################################################################################
# HyperHEN shared ptrace + ELF inject primitives (single authid policy).
###################################################################################################

cmake_minimum_required(VERSION 3.20)

set(basename "onion_elfldr")
project(${basename} C)

set(D_CWD "${PROJECT_SOURCE_DIR}")

onion_module_prologue(C_STANDARD 17)

add_library(${PROJECT_NAME} STATIC
  source/pt.c
  source/elfldr.c
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_POSIX_SOURCE -D_POSIX_C_SOURCE=200112 -D__BSD_VISIBLE=1 -D__XSI_VISIBLE=500")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-builtin -nostdlib -Wall -fPIC -gfull -O3")

target_include_directories(${PROJECT_NAME}
  PUBLIC  "${D_CWD}/include"
  PRIVATE "${D_CWD}/../include"
  "${PROJECT_ROOT}/libonion_platform/include"
)

# Authid policy: no per-syscall flip in pt.c; injectors elevate once at entry.
target_compile_definitions(${PROJECT_NAME} PRIVATE ONION_ELFLDR_NO_PER_CALL_AUTHID=1)

