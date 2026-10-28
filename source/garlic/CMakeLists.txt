cmake_minimum_required(VERSION 3.20)

project(hyperhen_garlic_savemgr C)
onion_module_prologue(C_STANDARD 11)
onion_output_dirs()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(_garlic_src "${HYPERHEN_GARLIC_SOURCE_DIR}/src")
foreach(_garlic_input IN ITEMS main.c sqlite3.c sqlite3.h ui.html fw_offsets_guard.h)
  if(NOT EXISTS "${_garlic_src}/${_garlic_input}")
    message(FATAL_ERROR "Missing Garlic SaveMgr source input: ${_garlic_src}/${_garlic_input}")
  endif()
endforeach()

set(_garlic_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_garlic_ui_header "${_garlic_generated_dir}/ui.h")
add_custom_command(
  OUTPUT "${_garlic_ui_header}"
  COMMAND "${Python3_EXECUTABLE}" "${HYPERHEN_ROOT}/scripts/embed_garlic_ui.py"
    --input "${_garlic_src}/ui.html" --output "${_garlic_ui_header}"
  DEPENDS "${HYPERHEN_ROOT}/scripts/embed_garlic_ui.py" "${_garlic_src}/ui.html"
  COMMENT "Embedding Garlic SaveMgr web interface"
  VERBATIM)
add_custom_target(garlic_savemgr_assets DEPENDS "${_garlic_ui_header}")

add_executable(garlic_savemgr
  "${_garlic_src}/main.c"
  "${_garlic_src}/sqlite3.c"
  "${_garlic_ui_header}")
set_target_properties(garlic_savemgr PROPERTIES OUTPUT_NAME "garlic-savemgr.elf")

# Garlic is third-party C source. Keep its warning policy independent from the
# stricter first-party targets while preserving the same PS5 target/CRT path.
set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} -fPIC -fstack-protector-all -O2 -Wall")
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG")
target_compile_definitions(garlic_savemgr PRIVATE
  _BSD_SOURCE
  SQLITE_OMIT_LOAD_EXTENSION
  SQLITE_THREADSAFE=0
  SQLITE_OMIT_WAL)
set_source_files_properties("${_garlic_src}/sqlite3.c" PROPERTIES COMPILE_OPTIONS "-w")
target_include_directories(garlic_savemgr PRIVATE
  "${_garlic_generated_dir}"
  "${_garlic_src}"
  "${PS5_PAYLOAD_SDK}/target/include")
target_link_directories(garlic_savemgr PRIVATE
  "${HYPERHEN_PS5_STUB_DIR}"
  "${PS5_PAYLOAD_SDK}/target/lib")
target_link_libraries(garlic_savemgr PRIVATE
  kernel_sys kernel SceSystemService SceUserService SceFsInternalForVsh SceRegMgr)
add_dependencies(garlic_savemgr garlic_savemgr_assets)
onion_strip_debug(garlic_savemgr)
