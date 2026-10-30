cmake_minimum_required (VERSION 3.20)

set(basename "daemon")
project(${basename} C CXX ASM)

onion_module_prologue(C_STANDARD 11 CXX_STANDARD 20)

set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
onion_output_dirs()
set(D_SRC ${D_CWD}/source)

add_executable(${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}.elf")

# Finalize main target sources
target_compile_options(${PROJECT_NAME} PUBLIC
	$<$<COMPILE_LANGUAGE:C>:${C_DEFS} ${C_FLAGS}>
	$<$<COMPILE_LANGUAGE:CXX>:${CXX_DEFS} ${CXX_FLAGS}>
	$<$<COMPILE_LANGUAGE:ASM>:${ASM_FLAGS}>
)
target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--gc-sections" "-Wl,--icf=safe")

file(GLOB SrcFiles CONFIGURE_DEPENDS ${D_SRC}/*.c ${D_SRC}/*.cpp ${D_SRC}/../include/*.h ${D_SRC}/../include/*.hpp ${D_SRC}/*.s ${HYPERHEN_THIRD_PARTY_DIR}/cjson/*.cpp ${HYPERHEN_THIRD_PARTY_DIR}/cjson/*.hpp)

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-keyword-macro -fPIC -fPIE -march=znver2 -Wall -Werror -fstack-protector-all -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -gfull -gdwarf-2 -pedantic -pedantic-errors")
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

target_sources(${PROJECT_NAME} PRIVATE ${SrcFiles} "${HYPERHEN_COMMON_DIR}/elfldr_remote.c")
# .incbin in embedded_payloads.c is invisible to the compiler dependency scanner.
# Without OBJECT_DEPENDS, embedded payload rebuilds do not re-embed into daemon.elf.
if(DEFINED HYPERHEN_OUT_BIN)
	set(_hyperhen_util_elf "${HYPERHEN_OUT_BIN}/util.elf")
	set(_hyperhen_elfldr_elf "${HYPERHEN_OUT_BIN}/hyperhen_elfldr.elf")
else()
	set(_hyperhen_util_elf "${PROJECT_ROOT}/bin/util.elf")
	set(_hyperhen_elfldr_elf "${PROJECT_ROOT}/bin/hyperhen_elfldr.elf")
endif()
set(_hyperhen_uses_stable_util FALSE)
if(HYPERHEN_RELEASE_COMPONENT_DIR)
  # Hyper's installer belongs to the daemon. Keep the hardware-tested util
  # byte-exact even when the assistant and its catalog are integrated.
  set(_hyperhen_util_elf "${HYPERHEN_RELEASE_COMPONENT_DIR}/util.elf")
  if(NOT EXISTS "${_hyperhen_util_elf}")
    message(FATAL_ERROR "Missing pinned util ELF: ${_hyperhen_util_elf}")
  endif()
  set(_hyperhen_uses_stable_util TRUE)
endif()
set(HYPERHEN_STABLE_INNER_LOADER_ELF "" CACHE FILEPATH
  "Byte-exact private recovery loader retained inside the daemon")
if(HYPERHEN_STABLE_INNER_LOADER_ELF)
  set(_hyperhen_elfldr_elf "${HYPERHEN_STABLE_INNER_LOADER_ELF}")
  if(NOT EXISTS "${_hyperhen_elfldr_elf}")
    message(FATAL_ERROR "Missing pinned private loader: ${_hyperhen_elfldr_elf}")
  endif()
endif()
# R39: pin the tested ShellUI while rebuilding only the daemon.
set(_hyperhen_shellui_elf "${HYPERHEN_OUT_BIN}/shellui.elf")
set(HYPERHEN_STABLE_SHELLUI_ELF "" CACHE FILEPATH
  "Byte-exact ShellUI retained inside the daemon")
if(HYPERHEN_STABLE_SHELLUI_ELF)
  set(_hyperhen_shellui_elf "${HYPERHEN_STABLE_SHELLUI_ELF}")
  if(NOT EXISTS "${_hyperhen_shellui_elf}")
    message(FATAL_ERROR "Missing pinned ShellUI ELF: ${_hyperhen_shellui_elf}")
  endif()
endif()
set_source_files_properties(
	${D_SRC}/embedded_payloads.c
	PROPERTIES OBJECT_DEPENDS
		"${_hyperhen_shellui_elf};${_hyperhen_util_elf};${_hyperhen_elfldr_elf}"
)
target_compile_definitions(${PROJECT_NAME} PRIVATE
		HYPERHEN_SHELLUI_ELF="${_hyperhen_shellui_elf}"
		HYPERHEN_UTIL_ELF="${_hyperhen_util_elf}"
		HYPERHEN_ELFLDR_ELF="${_hyperhen_elfldr_elf}"
)
if(HYPERHEN_INTEGRATED_HYPER)
  target_compile_definitions(${PROJECT_NAME} PRIVATE
    HYPERHEN_INTEGRATED_HYPER=1 HYPER_ASSISTANT_ELF="${HYPER_ASSISTANT_ELF}"
    HYPER_VOICES_ZIP="${HYPER_VOICES_ZIP}")
  set_property(SOURCE ${D_SRC}/embedded_payloads.c APPEND PROPERTY
    OBJECT_DEPENDS "${HYPER_ASSISTANT_ELF}")

  # Compile only Hyper's installer/transport into this process. Advanced and
  # HyperDeck continue to use the untouched release util and private loader.
  set(_hyper_installer_source "${PROJECT_ROOT}/util/source/hyper_installer.cpp")
  set(_hyper_installer_engine "${PROJECT_ROOT}/util/source/hyper_installer_engine.cpp")
  set(_hyper_http_source "${PROJECT_ROOT}/util/source/cheats/sync/http_transport_ps5.cpp")
  set(_hyper_miniz_source "${HYPERHEN_THIRD_PARTY_DIR}/cheat_support/miniz.c")
  set(_hyper_sha256_source "${HYPERHEN_THIRD_PARTY_DIR}/cheat_support/sha256.c")
  target_sources(${PROJECT_NAME} PRIVATE
    "${_hyper_installer_source}" "${_hyper_installer_engine}"
    "${_hyper_http_source}" "${_hyper_miniz_source}" "${_hyper_sha256_source}")
  set(_hyper_curl_root "${PS5_PAYLOAD_SDK}/target/user/homebrew")
  set(_hyper_curl_ca "${_hyper_curl_root}/etc/ca-bundle.crt")
  if(NOT EXISTS "${_hyper_curl_root}/include/curl/curl.h" OR
     NOT EXISTS "${_hyper_curl_root}/lib/libcurl.a" OR
     NOT EXISTS "${_hyper_curl_ca}")
    message(FATAL_ERROR "Hyper installer requires the local SDK libcurl and CA bundle")
  endif()
  target_compile_definitions(${PROJECT_NAME} PRIVATE
    CURL_STATICLIB ONION_CURL_CA_BUNDLE="${_hyper_curl_ca}")
  target_include_directories(${PROJECT_NAME} PRIVATE
    "${PROJECT_ROOT}/util/include"
    "${HYPERHEN_THIRD_PARTY_DIR}/cheat_support")
  target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    "${_hyper_curl_root}/include")
  target_link_directories(${PROJECT_NAME} PRIVATE
    "${HYPERHEN_PS5_STUB_DIR}" "${_hyper_curl_root}/lib"
    "${PS5_PAYLOAD_SDK}/target/lib")
  target_link_libraries(${PROJECT_NAME} PRIVATE
    SceNet curl psl ssl crypto zstd z)
  set_source_files_properties("${_hyper_installer_source}" PROPERTIES
    OBJECT_DEPENDS "${HYPER_VOICES_ZIP}")
  set_source_files_properties("${_hyper_http_source}" PROPERTIES
    OBJECT_DEPENDS "${_hyper_curl_ca}")
  set_source_files_properties("${_hyper_installer_engine}" PROPERTIES
    COMPILE_FLAGS "-Wno-unused-function -Wno-sign-compare -Wno-unused-parameter")
  set_source_files_properties("${_hyper_miniz_source}" "${_hyper_sha256_source}"
    PROPERTIES COMPILE_FLAGS
      "-Wno-error -Wno-unused-function -Wno-sign-compare -Wno-unused-parameter")
endif()
onion_target_debug_define(${PROJECT_NAME})
# Keep process-local daemon headers (notably ipc.hpp) ahead of
# the installer's util headers, which are shared only by the added sources.
target_include_directories(${PROJECT_NAME} BEFORE PRIVATE
	"${D_CWD}/include"
	"${HYPERHEN_THIRD_PARTY_DIR}/cjson"
	"${PROJECT_ROOT}/libonion_settings/include"
	"${PROJECT_ROOT}/libonion_ipc/include"
	"${PROJECT_ROOT}/libonion_proc/include"
	"${PROJECT_ROOT}/libonion_fps/include"
	"${PROJECT_ROOT}/libonion_lifecycle/include"
	"${PROJECT_ROOT}/libonion_sprx/include"
	"${PROJECT_ROOT}/libonion_platform/include"
	"${PROJECT_ROOT}/libonion_plugin_session/include"
	"${PROJECT_ROOT}/libonion_plugin_ui/include"
	"${PROJECT_ROOT}/libonion_plugin_bridge/include"
	"${PROJECT_ROOT}/libonion_plugin_manager/include"
    "${HYPERHEN_THIRD_PARTY_DIR}/tachen-plugin-runtime"
	"${PROJECT_ROOT}/include"
)
target_link_directories	(${PROJECT_NAME} PUBLIC "${HYPERHEN_PS5_STUB_DIR}")
# A source-built plugin runtime deliberately retains the stable util ELF when
# a release-component set is selected.  Do not pull the unrelated source util
# target into that compatibility build: it is not embedded by daemon.elf and
# can make a focused ShellUI/runtime rebuild depend on optional HTTP catalog
# toolchain assets.
set(_daemon_dependencies
  hyperhen_version NineS hijacker
  onion_settings onion_ipc onion_proc onion_fps onion_platform onion_ready
  onion_lifecycle onion_plugin_session onion_plugin_bridge onion_plugin_manager
  onion_elfldr onion_sprx)
if(NOT HYPERHEN_STABLE_SHELLUI_ELF)
  list(APPEND _daemon_dependencies shellui)
endif()
if(NOT HYPERHEN_STABLE_INNER_LOADER_ELF)
  list(APPEND _daemon_dependencies hyperhen_elfldr_server)
endif()
if(NOT _hyperhen_uses_stable_util)
  list(APPEND _daemon_dependencies util)
endif()
add_dependencies(${PROJECT_NAME} ${_daemon_dependencies})
# Supervises private :9020 via external :9021, then restarts embedded util via :9020.
target_link_libraries	(${PROJECT_NAME} PUBLIC onion_ipc onion_settings onion_proc onion_fps onion_platform onion_ready onion_lifecycle onion_plugin_session onion_plugin_bridge onion_plugin_manager onion_elfldr onion_sprx hijacker SceSystemService SceNotification SceUserService SceNetCtl SceSysCore kernel_sys NineS)

onion_strip_debug(${PROJECT_NAME})
