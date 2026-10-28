cmake_minimum_required(VERSION 3.20)
project(hyperdeck_websrv C CXX)
onion_module_prologue(C_STANDARD 11 CXX_STANDARD 20)
onion_output_dirs()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_http "${CMAKE_BINARY_DIR}/vendor/websrv-http")
file(MAKE_DIRECTORY "${_http}/include")
option(HYPERHEN_P5_REUSE_STABLE_HYPERDECK
  "Use already-built byte-exact R20 web library and engines for the isolated P5 build" OFF)
if(NOT HYPERHEN_P5_REUSE_STABLE_HYPERDECK)
add_custom_command(
  OUTPUT "${_http}/lib/libmicrohttpd.a" "${_http}/include/microhttpd.h"
  COMMAND bash "${HYPERHEN_ROOT}/scripts/build_websrv_http.sh"
    "${HYPERHEN_THIRD_PARTY_DIR}/libmicrohttpd/libmicrohttpd-1.0.1.tar.gz" "${_http}"
  DEPENDS "${HYPERHEN_ROOT}/scripts/build_websrv_http.sh"
    "${HYPERHEN_THIRD_PARTY_DIR}/libmicrohttpd/libmicrohttpd-1.0.1.tar.gz"
  COMMENT "Building the source-pinned websrv HTTP library" VERBATIM)
endif()
add_custom_target(hyperdeck_http_library DEPENDS "${_http}/lib/libmicrohttpd.a" "${_http}/include/microhttpd.h")
set(_engine_source "${HYPERHEN_THIRD_PARTY_DIR}/hyperdeck-tools")
set(_engine_output "${CMAKE_BINARY_DIR}/vendor/hyperdeck-engines")
set(_game_engine "${_engine_output}/game-compressor.elf")
set(_file_engine "${_engine_output}/web-file-manager.elf")
set(_cheat_engine "${_engine_output}/cheatrunner.elf")
set(_ps5upload_engine "${_engine_output}/ps5upload.elf")
file(GLOB_RECURSE _engine_inputs CONFIGURE_DEPENDS
  "${_engine_source}/game-compressor/Makefile"
  "${_engine_source}/game-compressor/src/*"
  "${_engine_source}/game-compressor/assets/*"
  "${_engine_source}/game-compressor/hyperhen/*"
  "${_engine_source}/game-compressor/*.py"
  "${_engine_source}/web-file-manager/Makefile"
  "${_engine_source}/web-file-manager/src/*"
  "${_engine_source}/web-file-manager/assets/*"
  "${_engine_source}/web-file-manager/hyperhen/*"
  "${_engine_source}/web-file-manager/patches/*"
  "${_engine_source}/web-file-manager/vendor/libsmb2/*"
  "${_engine_source}/web-file-manager/*.py"
  "${_engine_source}/web-file-manager/.gitmodules"
  "${_engine_source}/cheatrunner/Makefile"
  "${_engine_source}/cheatrunner/src/*"
  "${_engine_source}/cheatrunner/tools/*"
  "${_engine_source}/cheatrunner/CheatRunner.png"
  "${_engine_source}/ps5upload/payload/Makefile"
  "${_engine_source}/ps5upload/payload/include/*"
  "${_engine_source}/ps5upload/payload/src/*"
  "${_engine_source}/ps5upload/payload/ava1/*"
  "${_engine_source}/ps5upload/payload/third_party/*"
  "${_engine_source}/ps5upload/hyperhen/generated/*")
if(NOT HYPERHEN_P5_REUSE_STABLE_HYPERDECK)
add_custom_command(
  OUTPUT "${_game_engine}" "${_file_engine}" "${_cheat_engine}" "${_ps5upload_engine}"
  COMMAND bash "${HYPERHEN_ROOT}/scripts/build_hyperdeck_engines.sh"
    "${_engine_source}" "${_engine_output}" "${_http}" "${CMAKE_COMMAND}"
  DEPENDS "${HYPERHEN_ROOT}/scripts/build_hyperdeck_engines.sh"
    "${HYPERHEN_ROOT}/scripts/verify_payload_elf.py"
    "${_http}/lib/libmicrohttpd.a" "${_http}/include/microhttpd.h"
    ${_engine_inputs}
  COMMENT "Building complete native HyperDeck tool engines without separate Media tiles"
  VERBATIM)
endif()
add_custom_target(hyperdeck_native_engines DEPENDS
  "${_game_engine}" "${_file_engine}" "${_cheat_engine}" "${_ps5upload_engine}")

set(HYPERHEN_STABLE_PKG_HELPER_ELF "" CACHE FILEPATH
  "Optional byte-exact package helper retained inside a version-only HyperDeck update")
if(HYPERHEN_STABLE_PKG_HELPER_ELF)
  if(NOT EXISTS "${HYPERHEN_STABLE_PKG_HELPER_ELF}")
    message(FATAL_ERROR "Missing pinned HyperDeck package helper: ${HYPERHEN_STABLE_PKG_HELPER_ELF}")
  endif()
  set(_pkg_helper_elf "${HYPERHEN_STABLE_PKG_HELPER_ELF}")
else()
  add_executable(hyperhen_pkg_install "${HYPERHEN_THIRD_PARTY_DIR}/websrv/homebrew/PKGInstall/pkg_install.c")
  set_target_properties(hyperhen_pkg_install PROPERTIES OUTPUT_NAME "hyperhen-pkg-install.elf")
  target_link_directories(hyperhen_pkg_install PRIVATE "${HYPERHEN_PS5_STUB_DIR}")
  target_link_libraries(hyperhen_pkg_install PRIVATE SceIpmi SceAppInstUtil)
  set(_pkg_helper_elf "${HYPERHEN_OUT_BIN}/hyperhen-pkg-install.elf")
endif()
add_library(hyperdeck_http STATIC IMPORTED)
set_target_properties(hyperdeck_http PROPERTIES IMPORTED_LOCATION "${_http}/lib/libmicrohttpd.a")
set(_assets "${CMAKE_CURRENT_SOURCE_DIR}/assets")
file(GLOB_RECURSE _asset_files CONFIGURE_DEPENDS "${_assets}/*")
set(_embedded "${CMAKE_CURRENT_BINARY_DIR}/hyperdeck_assets.c")
add_custom_command(OUTPUT "${_embedded}"
  COMMAND "${Python3_EXECUTABLE}" "${HYPERHEN_ROOT}/scripts/embed_hyperdeck.py"
    --assets "${_assets}" --logo "${HYPERHEN_ROOT}/assets/logo.png" --output "${_embedded}"
  DEPENDS "${HYPERHEN_ROOT}/scripts/embed_hyperdeck.py" ${_asset_files}
    "${HYPERHEN_ROOT}/assets/logo.png"
  VERBATIM)
add_executable(hyperdeck_websrv server.cpp backend_ps5.cpp engine_runtime_ps5.cpp pkg_catalog.cpp pkg_transport_ps5.cpp json.cpp
  "${_embedded}" "${HYPERHEN_THIRD_PARTY_DIR}/websrv/src/asset.c"
  "${HYPERHEN_COMMON_DIR}/elfldr_remote.c")
set_target_properties(hyperdeck_websrv PROPERTIES OUTPUT_NAME "hyperdeck-websrv.elf")
set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} -fPIC -fstack-protector-all -O1 -Wall -Wextra")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-exceptions")
target_include_directories(hyperdeck_websrv PRIVATE include "${_http}/include"
  "${HYPERHEN_THIRD_PARTY_DIR}/websrv/src" "${PROJECT_ROOT}/include"
  "${HYPERHEN_GENERATED_INCLUDE_DIR}")
target_link_directories(hyperdeck_websrv PRIVATE "${HYPERHEN_PS5_STUB_DIR}")
target_link_libraries(hyperdeck_websrv PRIVATE hyperdeck_http onion_elfldr onion_ipc onion_payload
  onion_proc onion_settings onion_ready onion_platform kernel_sys SceSystemService SceNetCtl SceUserService)
add_dependencies(hyperdeck_websrv hyperdeck_http_library hyperdeck_native_engines hyperhen_version)
if(NOT HYPERHEN_STABLE_PKG_HELPER_ELF)
  add_dependencies(hyperdeck_websrv hyperhen_pkg_install)
endif()
target_compile_definitions(hyperdeck_websrv PRIVATE
  HYPERHEN_PKG_HELPER_ELF="${_pkg_helper_elf}"
  HYPERHEN_GAME_COMPRESSOR_ELF="${_game_engine}"
  HYPERHEN_WEB_FILE_MANAGER_ELF="${_file_engine}"
  HYPERHEN_CHEATRUNNER_ELF="${_cheat_engine}"
  HYPERHEN_PS5UPLOAD_ELF="${_ps5upload_engine}")
set_source_files_properties(pkg_transport_ps5.cpp PROPERTIES OBJECT_DEPENDS "${_pkg_helper_elf}")
set_source_files_properties(engine_runtime_ps5.cpp PROPERTIES OBJECT_DEPENDS
  "${_game_engine};${_file_engine};${_cheat_engine};${_ps5upload_engine}")
onion_strip_debug(hyperdeck_websrv)
