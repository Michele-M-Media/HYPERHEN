cmake_minimum_required(VERSION 3.20)
set(basename "onion_proc")
project(${basename} C CXX)
set(D_CWD "${PROJECT_SOURCE_DIR}")
onion_module_prologue(C_STANDARD 11 CXX_STANDARD 20)
add_library(${PROJECT_NAME} STATIC
  source/proc.cpp
  source/ucred.cpp
  source/proc_query.cpp
  source/proc_dmap.cpp
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")
set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} -fPIC -fstack-protector-all")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wextra -Werror -fno-exceptions")
target_include_directories(${PROJECT_NAME}
  PUBLIC  "${D_CWD}/include"
  PRIVATE "${D_CWD}/../include"
  "${PROJECT_ROOT}/libonion_platform/include"
)
