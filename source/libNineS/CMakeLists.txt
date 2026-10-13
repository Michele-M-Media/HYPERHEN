###################################################################################################
# PS5SDK - Example: pipe pirate
# Uses the read/write primitive to read and write some kernel data.
# @authors ChendoChap, Specter, Znullptr
###################################################################################################

cmake_minimum_required (VERSION 3.20)

set(basename "NineS")
project(${basename} C)

set(D_CWD "${PROJECT_SOURCE_DIR}")

onion_module_prologue(C_STANDARD 17)

set(D_SRC ${D_CWD}/src)

set(SrcFiles
  ${D_SRC}/injector.c
  ${D_SRC}/main.c
  ${D_SRC}/notify.c
)
# pt.c / elfldr.c → libonion_elfldr; proc/ucred → libonion_proc

add_library(${PROJECT_NAME} STATIC ${SrcFiles})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

# Finalize main target sources
target_compile_options(${PROJECT_NAME} PUBLIC
	$<$<COMPILE_LANGUAGE:C>:${C_DEFS} ${C_FLAGS}>
)

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_POSIX_SOURCE -D_POSIX_C_SOURCE=200112 -D__BSD_VISIBLE=1 -D__XSI_VISIBLE=500")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-builtin -nostdlib -Wall") #  -nostartfiles
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -gfull -O3 -march=znver2 -Wall -Werror")
target_include_directories(${PROJECT_NAME} PRIVATE
	"${PROJECT_ROOT}/libonion_platform/include"
  "${D_CWD}/include"
  "${CMAKE_SOURCE_DIR}/libonion_proc/include"
  "${CMAKE_SOURCE_DIR}/libonion_elfldr/include"
  "${CMAKE_SOURCE_DIR}/include"
)
# When built as subproject of source/, CMAKE_SOURCE_DIR is source/
# Prefer PROJECT_ROOT if parent set it.
if(DEFINED PROJECT_ROOT)
  target_include_directories(${PROJECT_NAME} PRIVATE
	"${PROJECT_ROOT}/libonion_platform/include"
    "${PROJECT_ROOT}/libonion_proc/include"
    "${PROJECT_ROOT}/libonion_elfldr/include"
    "${PROJECT_ROOT}/include"
  )
endif()
if(TARGET onion_proc)
  add_dependencies(${PROJECT_NAME} onion_proc)
endif()
if(TARGET onion_elfldr)
  add_dependencies(${PROJECT_NAME} onion_elfldr)
  target_link_libraries(${PROJECT_NAME} PUBLIC onion_elfldr)
endif()
