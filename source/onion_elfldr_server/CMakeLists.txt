cmake_minimum_required(VERSION 3.20)

set(basename "hyperhen_elfldr_server")
project(${basename} C)

set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
onion_output_dirs()

onion_module_prologue(C_STANDARD 17)

add_executable(${PROJECT_NAME} source/main.c)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "hyperhen_elfldr.elf")

# User-facing LAN loader controlled from HyperHEN Tools.  It is intentionally
# a separate process/port from the private :9020 runtime loader.
set(DEBUG_PROJECT_NAME "hyperhen_debug_elfldr_server")
add_executable(${DEBUG_PROJECT_NAME} source/main.c)
set_target_properties(${DEBUG_PROJECT_NAME} PROPERTIES
  OUTPUT_NAME "hyperhen_debug_elfldr.elf")
target_compile_definitions(${DEBUG_PROJECT_NAME} PRIVATE
  HYPERHEN_ELFLDR_DEBUG_SERVICE=1)

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_POSIX_SOURCE -D_POSIX_C_SOURCE=200809L -D__BSD_VISIBLE=1 -D__XSI_VISIBLE=500")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-keyword-macro -fPIC -fPIE -march=znver2 -Wall -Werror -fstack-protector-all -O2")

foreach(_elfldr_target IN ITEMS ${PROJECT_NAME} ${DEBUG_PROJECT_NAME})
  target_include_directories(${_elfldr_target} PRIVATE
    "${PROJECT_ROOT}/include"
    "${PROJECT_ROOT}/libonion_elfldr/include"
    "${PROJECT_ROOT}/libonion_platform/include"
  )
  target_link_directories(${_elfldr_target} PUBLIC
    "${HYPERHEN_PS5_STUB_DIR}")
  add_dependencies(${_elfldr_target} onion_elfldr)
  target_link_libraries(${_elfldr_target} PUBLIC onion_platform onion_elfldr)
  onion_strip_debug(${_elfldr_target})
endforeach()
