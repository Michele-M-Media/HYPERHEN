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
if(HYPERHEN_RELEASE_COMPONENT_DIR AND HYPERHEN_SOURCE_PLUGIN_RUNTIME)
  # Preserve the tested util while replacing ONLY the plugin-aware daemon,
  # Toolbox module and private loader. The old util is not part of this fix.
  set(_hyperhen_util_elf "${HYPERHEN_RELEASE_COMPONENT_DIR}/util.elf")
endif()
set(HYPERHEN_STABLE_INNER_LOADER_ELF "" CACHE FILEPATH
  "Byte-exact private recovery loader retained inside the daemon")
if(HYPERHEN_STABLE_INNER_LOADER_ELF)
  set(_hyperhen_elfldr_elf "${HYPERHEN_STABLE_INNER_LOADER_ELF}")
  if(NOT EXISTS "${_hyperhen_elfldr_elf}")
    message(FATAL_ERROR "Missing pinned private loader: ${_hyperhen_elfldr_elf}")
  endif()
endif()
set_source_files_properties(
	${D_SRC}/embedded_payloads.c
	PROPERTIES OBJECT_DEPENDS
		"${HYPERHEN_OUT_BIN}/shellui.elf;${_hyperhen_util_elf};${_hyperhen_elfldr_elf}"
)
target_compile_definitions(${PROJECT_NAME} PRIVATE
		HYPERHEN_SHELLUI_ELF="${HYPERHEN_OUT_BIN}/shellui.elf"
		HYPERHEN_UTIL_ELF="${_hyperhen_util_elf}"
		HYPERHEN_ELFLDR_ELF="${_hyperhen_elfldr_elf}"
)
onion_target_debug_define(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE
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
  hyperhen_version NineS shellui hijacker
  onion_settings onion_ipc onion_proc onion_fps onion_platform onion_ready
  onion_lifecycle onion_plugin_session onion_plugin_bridge onion_plugin_manager
  onion_elfldr onion_sprx)
if(NOT HYPERHEN_STABLE_INNER_LOADER_ELF)
  list(APPEND _daemon_dependencies hyperhen_elfldr_server)
endif()
if(NOT (HYPERHEN_RELEASE_COMPONENT_DIR AND HYPERHEN_SOURCE_PLUGIN_RUNTIME))
  list(APPEND _daemon_dependencies util)
endif()
add_dependencies(${PROJECT_NAME} ${_daemon_dependencies})
# Supervises private :9020 via external :9021, then restarts embedded util via :9020.
target_link_libraries	(${PROJECT_NAME} PUBLIC onion_ipc onion_settings onion_proc onion_fps onion_platform onion_ready onion_lifecycle onion_plugin_session onion_plugin_bridge onion_plugin_manager onion_elfldr onion_sprx hijacker SceSystemService SceNotification SceUserService SceNetCtl SceSysCore kernel_sys NineS)

onion_strip_debug(${PROJECT_NAME})
