###################################################################################################
# PS5SDK - Example: pipe pirate
# Uses the read/write primitive to read and write some kernel data.
# @authors ChendoChap, Specter, Znullptr
###################################################################################################

cmake_minimum_required (VERSION 3.20)

set(basename "hijacker")
project(${basename} C CXX ASM)

onion_module_prologue(C_STANDARD 11 CXX_STANDARD 20)
if ("${CMAKE_CXX_STANDARD}" STREQUAL "")
endif()

set(D_CWD "${PROJECT_SOURCE_DIR}")

set(D_SRC ${D_CWD}/source)

file(GLOB SrcFiles CONFIGURE_DEPENDS ${D_SRC}/*.c ${D_SRC}/*.cpp ${D_SRC}/**/*.cpp ${D_SRC}/*.h ${D_SRC}/*.hpp ${D_SRC}/**/*.hpp ${D_SRC}/*.s)

add_library(${PROJECT_NAME} STATIC ${SrcFiles})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

# Finalize main target sources
target_compile_options(${PROJECT_NAME} PUBLIC
	$<$<COMPILE_LANGUAGE:C>:${C_DEFS} ${C_FLAGS}>
	$<$<COMPILE_LANGUAGE:CXX>:${CXX_DEFS} ${CXX_FLAGS}>
	$<$<COMPILE_LANGUAGE:ASM>:${ASM_FLAGS}>
)

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC  -fstack-protector-all")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -O3 -march=znver2 -Wall -Wextra -Wmove -Wmost -Werror -fno-exceptions")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g0 -O0")
target_include_directories(${PROJECT_NAME}
	PUBLIC  "${D_CWD}/include"
	PRIVATE "${D_CWD}/../include"
	PRIVATE "${D_CWD}/../libonion_platform/include"
	PRIVATE "${HYPERHEN_THIRD_PARTY_DIR}/sha1"
)
