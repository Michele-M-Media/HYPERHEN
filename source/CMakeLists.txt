cmake_minimum_required (VERSION 3.20)

project("HyperHEN")

set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(OnionTarget)
get_filename_component(HYPERHEN_ROOT "${PROJECT_ROOT}/.." ABSOLUTE)
set(HYPERHEN_THIRD_PARTY_DIR "${HYPERHEN_ROOT}/third_party" CACHE PATH
	"Third-party dependency source directory")
set(HYPERHEN_COMMON_DIR "${PROJECT_ROOT}/common")
set(HYPERHEN_PS5_STUB_DIR "${PROJECT_ROOT}/platform/ps5/stubs")
set(HYPERHEN_CORE_SOURCE_DIR "${HYPERHEN_THIRD_PARTY_DIR}/kstuff-a53"
	CACHE PATH "Vendored kstuff+A53/PPR+BackPork source directory")
set(HYPERHEN_CORE_ELF "${CMAKE_BINARY_DIR}/vendor/hypercore/kstuff.elf"
	CACHE FILEPATH "Locally built HyperCore ELF embedded by the bootstrapper")
set(HYPERHEN_CORE_940_ELF "${HYPERHEN_CORE_ELF}"
	CACHE FILEPATH "Dedicated HyperCore ELF for firmware 9.40")
set(HYPERHEN_CORE_1270_ELF "${HYPERHEN_CORE_ELF}"
	CACHE FILEPATH "Dedicated source-built HyperCore ELF for firmware 12.70")
set(HYPERHEN_AUTOMOUNT_SOURCE_DIR
	"${HYPERHEN_THIRD_PARTY_DIR}/shadowmountplus"
	CACHE PATH "Vendored ShadowMountPlus source directory")
set(HYPERHEN_AUTOMOUNT_ELF
	"${CMAKE_BINARY_DIR}/vendor/automount/hyperhen_automount.elf"
	CACHE FILEPATH "Locally built HyperHEN Automount ELF embedded by the bootstrapper")
set(HYPERHEN_RELEASE_COMPONENT_DIR "" CACHE PATH
	"Optional directory of byte-exact stable release ELFs for existing firmware routes")
option(HYPERHEN_SOURCE_PLUGIN_RUNTIME
  "Embed source-built daemon/private loader with the updated plugin runtime" OFF)
set(HYPERHEN_STABLE_HYPERDECK_ELF "" CACHE FILEPATH
  "Optional byte-exact HyperDeck ELF retained while updating the plugin runtime")
set(HYPERHEN_HYPERVISOR_SOURCE_DIR
	"${HYPERHEN_THIRD_PARTY_DIR}/byepervisor-reborn"
	CACHE PATH "Vendored Byepervisor Reborn source directory")
set(HYPERHEN_HYPERVISOR_ELF
	"${CMAKE_BINARY_DIR}/vendor/hypervisor/hyperhen_hypervisor.elf"
	CACHE FILEPATH "Locally built hypervisor stage embedded by the bootstrapper")
set(HYPERHEN_HYPERVISOR_1270_ELF "${HYPERHEN_HYPERVISOR_ELF}"
	CACHE FILEPATH "Source-built Byepervisor stage containing the exact firmware 12.70 profile")
set(HYPERHEN_HYPERVISOR_5XX_SOURCE_DIR
	"${HYPERHEN_THIRD_PARTY_DIR}/bypervisor-5xx"
	CACHE PATH "Vendored 5.xx Byepervisor source directory")
set(HYPERHEN_HYPERVISOR_5XX_ELF
	"${CMAKE_BINARY_DIR}/vendor/hypervisor-5xx/hyperhen_hypervisor_5xx.elf"
	CACHE FILEPATH "Locally built 5.xx hypervisor stage embedded by the bootstrapper")
set(HYPERHEN_HYPERVISOR_940_ELF
	"${HYPERHEN_THIRD_PARTY_DIR}/byepervisor-940/byepervisor-940.elf"
	CACHE FILEPATH "Pinned prebuilt Byepervisor stage for firmware 9.40")
set(HYPERHEN_LEGACY_HEN_SOURCE_DIR
	"${HYPERHEN_THIRD_PARTY_DIR}/ps5-hen-dev"
	CACHE PATH "Vendored legacy PS5 HEN source directory")
set(HYPERHEN_LEGACY_HEN_ELF
	"${CMAKE_BINARY_DIR}/vendor/legacy-hen/hyperhen_legacy_hen.elf"
	CACHE FILEPATH "Locally built legacy HEN stage embedded by the bootstrapper")
set(HYPERHEN_PLUGIN_SDK_SOURCE_DIR
	"${HYPERHEN_THIRD_PARTY_DIR}/hyperhen-plugin-sdk"
	CACHE PATH "Vendored HyperHEN plugin client runtime source directory")
set(HYPERHEN_HYPERDPI_SOURCE_DIR
	"${HYPERHEN_THIRD_PARTY_DIR}/hyperdpi"
	CACHE PATH "Vendored HyperDPI source directory")
set(HYPERHEN_HYPERDPI_ELF
	"${CMAKE_BINARY_DIR}/bin/hyperdpi.elf"
	CACHE FILEPATH "Source-built HyperDPI ELF embedded by the bootstrapper")
set(HYPERHEN_LINUX_SOURCE_DIR
	"${HYPERHEN_THIRD_PARTY_DIR}/hyperlinux"
	CACHE PATH "Vendored HyperLinux/pLinux5-NG source directory")
set(HYPERHEN_LINUX_ELF
	"${CMAKE_BINARY_DIR}/vendor/hyperlinux/hyperlinux.elf"
	CACHE FILEPATH "Source-built HyperLinux ELF embedded by the bootstrapper")
set(HYPERHEN_LINUX_1360_ELF
	"${HYPERHEN_THIRD_PARTY_DIR}/hyperlinux-1360/1360linux.elf"
	CACHE FILEPATH "Dedicated prebuilt HyperLinux ELF for firmware 13.60")
set(HYPERHEN_GARLIC_SOURCE_DIR
	"${HYPERHEN_THIRD_PARTY_DIR}/garlic-savemgr"
	CACHE PATH "Vendored Garlic SaveMgr source directory")
set(HYPERHEN_GARLIC_ELF
	"${CMAKE_BINARY_DIR}/bin/garlic-savemgr.elf"
	CACHE FILEPATH "Source-built Garlic SaveMgr ELF embedded by the bootstrapper")
set(HYPERHEN_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/include")
set(HYPERHEN_VERSION_HEADER
	"${HYPERHEN_GENERATED_INCLUDE_DIR}/onion/version.h")
option(HYPERHEN_EXPERIMENTAL_HYPERSHELL
	"Build the isolated HyperShell dashboard and on-demand LiveShell engine" OFF)

# HyperCore is always built from the vendored source.  No release ELF is
# downloaded and no runtime override is accepted by the bootstrapper.
file(GLOB_RECURSE HYPERHEN_CORE_INPUTS CONFIGURE_DEPENDS
	"${HYPERHEN_CORE_SOURCE_DIR}/lib/*"
	"${HYPERHEN_CORE_SOURCE_DIR}/prosper0gdb/*"
	"${HYPERHEN_CORE_SOURCE_DIR}/ps5-kstuff/*"
	"${HYPERHEN_CORE_SOURCE_DIR}/ps5-kstuff-ldr/*")
add_custom_command(
	OUTPUT "${HYPERHEN_CORE_ELF}"
	COMMAND "${HYPERHEN_ROOT}/scripts/build_hypercore.sh"
		"${HYPERHEN_CORE_SOURCE_DIR}" "${HYPERHEN_CORE_ELF}"
	DEPENDS "${HYPERHEN_ROOT}/scripts/build_hypercore.sh"
		"${HYPERHEN_ROOT}/source/include/onion/firmware_capabilities.h"
		"${HYPERHEN_ROOT}/source/include/onion/ready.h"
		"${HYPERHEN_ROOT}/source/include/onion/system_tmp.h"
		${HYPERHEN_CORE_INPUTS}
	COMMENT "Building embedded HyperCore from vendored kstuff-a53 source"
	VERBATIM
)
add_custom_target(hypercore_payload DEPENDS "${HYPERHEN_CORE_ELF}")

file(GLOB_RECURSE HYPERHEN_AUTOMOUNT_INPUTS CONFIGURE_DEPENDS
	"${HYPERHEN_AUTOMOUNT_SOURCE_DIR}/Makefile"
	"${HYPERHEN_AUTOMOUNT_SOURCE_DIR}/config.ini.example"
	"${HYPERHEN_AUTOMOUNT_SOURCE_DIR}/assets/*"
	"${HYPERHEN_AUTOMOUNT_SOURCE_DIR}/include/*"
	"${HYPERHEN_AUTOMOUNT_SOURCE_DIR}/src/*"
	"${HYPERHEN_AUTOMOUNT_SOURCE_DIR}/web/*")
list(FILTER HYPERHEN_AUTOMOUNT_INPUTS EXCLUDE REGEX
	"/src/(notify_icon_asset|config_ini_example_asset|web_index_asset|shell_icon_param_asset)\\.c$")
add_custom_command(
	OUTPUT "${HYPERHEN_AUTOMOUNT_ELF}"
	COMMAND "${HYPERHEN_ROOT}/scripts/build_automount.sh"
		"${HYPERHEN_AUTOMOUNT_SOURCE_DIR}" "${HYPERHEN_AUTOMOUNT_ELF}"
	DEPENDS "${HYPERHEN_ROOT}/scripts/build_automount.sh"
		${HYPERHEN_AUTOMOUNT_INPUTS}
	COMMENT "Building embedded HyperHEN Automount from ShadowMountPlus source"
	VERBATIM
)
add_custom_target(automount_payload DEPENDS "${HYPERHEN_AUTOMOUNT_ELF}")

file(GLOB_RECURSE HYPERHEN_HYPERVISOR_INPUTS CONFIGURE_DEPENDS
	"${HYPERHEN_HYPERVISOR_SOURCE_DIR}/Makefile"
	"${HYPERHEN_HYPERVISOR_SOURCE_DIR}/*.c"
	"${HYPERHEN_HYPERVISOR_SOURCE_DIR}/*.h"
	"${HYPERHEN_HYPERVISOR_SOURCE_DIR}/*.S"
	"${HYPERHEN_HYPERVISOR_SOURCE_DIR}/*.ld"
	"${HYPERHEN_HYPERVISOR_SOURCE_DIR}/*.sh")
add_custom_command(
	OUTPUT "${HYPERHEN_HYPERVISOR_ELF}"
	COMMAND "${HYPERHEN_ROOT}/scripts/build_hypervisor.sh"
		"${HYPERHEN_HYPERVISOR_SOURCE_DIR}" "${HYPERHEN_HYPERVISOR_ELF}"
	DEPENDS "${HYPERHEN_ROOT}/scripts/build_hypervisor.sh"
		${HYPERHEN_HYPERVISOR_INPUTS}
	COMMENT "Building embedded HyperHEN hypervisor stage from Byepervisor source"
	VERBATIM
)
add_custom_target(hypervisor_payload DEPENDS "${HYPERHEN_HYPERVISOR_ELF}")

file(GLOB_RECURSE HYPERHEN_HYPERVISOR_5XX_INPUTS CONFIGURE_DEPENDS
	"${HYPERHEN_HYPERVISOR_5XX_SOURCE_DIR}/Makefile"
	"${HYPERHEN_HYPERVISOR_5XX_SOURCE_DIR}/*.c"
	"${HYPERHEN_HYPERVISOR_5XX_SOURCE_DIR}/*.h")
add_custom_command(
	OUTPUT "${HYPERHEN_HYPERVISOR_5XX_ELF}"
	COMMAND "${HYPERHEN_ROOT}/scripts/build_hypervisor_5xx.sh"
		"${HYPERHEN_HYPERVISOR_5XX_SOURCE_DIR}" "${HYPERHEN_HYPERVISOR_5XX_ELF}"
	DEPENDS "${HYPERHEN_ROOT}/scripts/build_hypervisor_5xx.sh"
		${HYPERHEN_HYPERVISOR_5XX_INPUTS}
	COMMENT "Building embedded HyperHEN 5.xx hypervisor stage from Byepervisor source"
	VERBATIM
)
add_custom_target(hypervisor_5xx_payload DEPENDS "${HYPERHEN_HYPERVISOR_5XX_ELF}")

file(GLOB_RECURSE HYPERHEN_LEGACY_HEN_INPUTS CONFIGURE_DEPENDS
	"${HYPERHEN_LEGACY_HEN_SOURCE_DIR}/Makefile"
	"${HYPERHEN_LEGACY_HEN_SOURCE_DIR}/*.c"
	"${HYPERHEN_LEGACY_HEN_SOURCE_DIR}/*.cpp"
	"${HYPERHEN_LEGACY_HEN_SOURCE_DIR}/*.h"
	"${HYPERHEN_LEGACY_HEN_SOURCE_DIR}/*.S"
	"${HYPERHEN_LEGACY_HEN_SOURCE_DIR}/*.s"
	"${HYPERHEN_LEGACY_HEN_SOURCE_DIR}/*.x")
add_custom_command(
	OUTPUT "${HYPERHEN_LEGACY_HEN_ELF}"
	COMMAND "${HYPERHEN_ROOT}/scripts/build_legacy_hen.sh"
		"${HYPERHEN_LEGACY_HEN_SOURCE_DIR}" "${HYPERHEN_LEGACY_HEN_ELF}"
	DEPENDS "${HYPERHEN_ROOT}/scripts/build_legacy_hen.sh"
		${HYPERHEN_LEGACY_HEN_INPUTS}
	COMMENT "Building embedded HyperHEN legacy HEN stage from ps5-hen source"
	VERBATIM
)
add_custom_target(legacy_hen_payload DEPENDS "${HYPERHEN_LEGACY_HEN_ELF}")

file(GLOB_RECURSE HYPERHEN_LINUX_INPUTS CONFIGURE_DEPENDS
	"${HYPERHEN_LINUX_SOURCE_DIR}/Makefile"
	"${HYPERHEN_LINUX_SOURCE_DIR}/*.c"
	"${HYPERHEN_LINUX_SOURCE_DIR}/*.h"
	"${HYPERHEN_LINUX_SOURCE_DIR}/*.S"
	"${HYPERHEN_LINUX_SOURCE_DIR}/*.ld"
	"${HYPERHEN_LINUX_SOURCE_DIR}/*.py")
list(FILTER HYPERHEN_LINUX_INPUTS EXCLUDE REGEX
	"/shellcode_(0607|hv|kernel|break_hv|hv_boot|hv_takeover)/(shellcode_0607|shellcode_hv|shellcode_kernel|break_hv|hv_boot|takeover)\\.h$")
add_custom_command(
	OUTPUT "${HYPERHEN_LINUX_ELF}"
	COMMAND "${HYPERHEN_ROOT}/scripts/build_hyperlinux.sh"
		"${HYPERHEN_LINUX_SOURCE_DIR}" "${HYPERHEN_LINUX_ELF}"
	DEPENDS "${HYPERHEN_ROOT}/scripts/build_hyperlinux.sh"
		${HYPERHEN_LINUX_INPUTS}
	COMMENT "Building embedded HyperLinux from vendored pLinux5-NG source"
	VERBATIM
)
add_custom_target(hyperlinux_payload DEPENDS "${HYPERHEN_LINUX_ELF}")

# Re-evaluate Git state on every build. The script preserves the header mtime
# when the version is unchanged, so incremental compilation remains cheap.
add_custom_target(hyperhen_version
	COMMAND "${CMAKE_COMMAND}"
		"-DREPO_ROOT=${HYPERHEN_ROOT}"
		"-DOUTPUT_FILE=${HYPERHEN_VERSION_HEADER}"
		-P "${PROJECT_ROOT}/cmake/GenerateVersionHeader.cmake"
	BYPRODUCTS "${HYPERHEN_VERSION_HEADER}"
	COMMENT "Generating HyperHEN build version"
	VERBATIM
)

# ---------------------------------------------------------------------------
# Artifact layout
#   ${CMAKE_BINARY_DIR}/bin  — ELFs (daemon, util, bootstrapper, HyperHEN, …)
#   ${CMAKE_BINARY_DIR}/lib  — built static libs (libonion_*, …)
# PS5 system-library stubs stay under source/platform/ps5/stubs.
#
# Default scripts/build.sh uses -B <repo>/build so products land at repo/build/.
# ---------------------------------------------------------------------------
set(HYPERHEN_OUT_BIN "${CMAKE_BINARY_DIR}/bin" CACHE PATH "ELF output directory")
set(HYPERHEN_OUT_LIB "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Built static-lib output directory")

file(MAKE_DIRECTORY "${HYPERHEN_OUT_BIN}" "${HYPERHEN_OUT_LIB}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${HYPERHEN_OUT_BIN})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${HYPERHEN_OUT_LIB})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${HYPERHEN_OUT_LIB})

include_directories(BEFORE "${HYPERHEN_GENERATED_INCLUDE_DIR}")
# First-party shared contracts — not SYSTEM, so warnings in them are visible.
include_directories("${PROJECT_ROOT}/include")
include_directories(SYSTEM "${PS5_PAYLOAD_SDK}")
include_directories(SYSTEM "${PS5_PAYLOAD_SDK}/include")

# Built libraries first, then PS5 system stubs.
link_directories("${HYPERHEN_OUT_LIB}" "${HYPERHEN_PS5_STUB_DIR}")

# HyperDPI is built from its GPL source and the pinned client runtime. The
# resulting ELF is an internal bootstrapper input; no runtime plugin file is
# downloaded or required under /data/HyperHEN/plugins.
add_subdirectory(
	"${HYPERHEN_PLUGIN_SDK_SOURCE_DIR}"
	"${CMAKE_BINARY_DIR}/vendor/hyperhen-plugin-sdk"
	EXCLUDE_FROM_ALL)
add_subdirectory(
	"${HYPERHEN_HYPERDPI_SOURCE_DIR}"
	"${CMAKE_BINARY_DIR}/vendor/hyperdpi")

add_subdirectory(libhijacker)
add_subdirectory(libNidResolver)
# add_subdirectory(libRPI)
# libSelfDecryptor removed from HyperHEN.
# Shared proc/ucred before NineS (inject path links these symbols).
add_subdirectory(libonion_proc)
add_subdirectory(libonion_fps)
add_subdirectory(libonion_platform)
add_subdirectory(libonion_payload)
add_subdirectory(libonion_conflict)
add_subdirectory(libonion_ipc)
add_subdirectory(libonion_settings)
add_subdirectory(libonion_detour)
add_subdirectory(libonion_ready)
add_subdirectory(libonion_lifecycle)
add_subdirectory(libonion_plugin_ui)
add_subdirectory(libonion_plugin_bridge)
add_subdirectory(libonion_plugin_session)
add_subdirectory(libonion_plugin_manager)
# Shared ptrace + ELF inject (authid elevated once at inject entry, not per syscall).
add_subdirectory(libonion_elfldr)
add_subdirectory(libonion_sprx)
add_subdirectory(onion_elfldr_server)
add_subdirectory(garlic)
add_subdirectory(libNineS)
if(HYPERHEN_EXPERIMENTAL_HYPERSHELL)
	add_subdirectory(
		"${HYPERHEN_THIRD_PARTY_DIR}/hyperdeck-tools/liveshell/src"
		"${CMAKE_BINARY_DIR}/vendor/hypershell"
		EXCLUDE_FROM_ALL)
endif()
add_subdirectory(shellui)
add_subdirectory(util)
add_subdirectory(daemon)
add_subdirectory(hyperdeck)
add_subdirectory(bootstrapper)
add_subdirectory(unpacker)
