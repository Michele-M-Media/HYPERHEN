# HyperHEN shared CMake helpers.
#
# Every module under source/ used to repeat the same prologue: language
# standards, the PS5 SDK header paths, a clang check, the cross-compile target
# triple, the output directories, and an objcopy strip step. That was 17 copies
# of the triple, 11 of the IS_SUBPROJECT dance, 9 clang checks and 6 strip
# blocks — all of which had to be kept in sync by hand.
#
# Modules now call:
#   onion_module_prologue()          # standards + SDK includes + clang check
#   onion_output_dirs()              # bin/ and lib/ layout
#   onion_strip_debug(<target>)      # post-build objcopy (ELF targets only)
#
# Compiler flags stay per-module: the modules genuinely differ (libhijacker
# builds -O3 -fno-exceptions, shellui adds -pedantic, libNidResolver is
# -nostdlib). ONION_PS5_TARGET_FLAGS holds only the part that is identical
# everywhere, so a module composes it with its own flags rather than retyping
# the triple.

include_guard(GLOBAL)

# Cross-compile target + platform defines. Identical in all 17 modules.
# NOTE: no -march here — a few modules place it at a different position in
# their flag string, so they append it themselves.
set(ONION_PS5_TARGET_FLAGS
	"--target=x86_64-sie-ps5 -DPPR -DPS5")

# Same, with -march=znver2 folded in, which is what most modules want.
set(ONION_PS5_TARGET_FLAGS_MARCH
	"--target=x86_64-sie-ps5 -march=znver2 -DPPR -DPS5")

# Language standards, PS5 SDK headers, and a compiler sanity check.
# A macro (not a function) so the CMAKE_* settings land in the caller's scope.
#
# Usage: onion_module_prologue([C_STANDARD <n>] [CXX_STANDARD <n>])
#
# The standards are opt-in because modules genuinely differ: the loader-side
# C modules (NidResolver, NineS, onion_elfldr, elfldr_server) are C17 while
# the rest are C11. Setting a default here would silently downgrade them.
macro(onion_module_prologue)
	cmake_parse_arguments(_onion "" "C_STANDARD;CXX_STANDARD" "" ${ARGN})
	if(DEFINED _onion_C_STANDARD)
		set(CMAKE_C_STANDARD ${_onion_C_STANDARD})
		set(CMAKE_C_STANDARD_REQUIRED ON)
	endif()
	if(DEFINED _onion_CXX_STANDARD)
		set(CMAKE_CXX_STANDARD ${_onion_CXX_STANDARD})
		set(CMAKE_CXX_STANDARD_REQUIRED ON)
		set(CMAKE_CXX_EXTENSIONS ON)
	endif()

	include_directories(SYSTEM "${PS5_PAYLOAD_SDK}")
	include_directories(SYSTEM "${PS5_PAYLOAD_SDK}/include")

	# Check whichever language the module actually enabled — asking about
	# CMAKE_CXX_COMPILER_ID in a `project(... C)` module reads as empty and
	# would fail the check spuriously.
	get_property(_onion_langs GLOBAL PROPERTY ENABLED_LANGUAGES)
	if("CXX" IN_LIST _onion_langs)
		set(_onion_compiler_id "${CMAKE_CXX_COMPILER_ID}")
	else()
		set(_onion_compiler_id "${CMAKE_C_COMPILER_ID}")
	endif()
	if(NOT "${_onion_compiler_id}" MATCHES "[Cc]lang")
		message(FATAL_ERROR
			"${PROJECT_NAME} must be built with clang! CompilerID: ${_onion_compiler_id}")
	endif()
	unset(_onion_langs)
	unset(_onion_compiler_id)

	message("========== build: ${PROJECT_NAME} ==========")
	set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endmacro()

# Artifact layout: ELFs to bin/, static libs to lib/. Falls back to a
# module-local directory for standalone (non-superbuild) configures.
macro(onion_output_dirs)
	if(DEFINED HYPERHEN_OUT_BIN)
		set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${HYPERHEN_OUT_BIN}")
	else()
		set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/../bin")
	endif()
	if(DEFINED HYPERHEN_OUT_LIB)
		set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${HYPERHEN_OUT_LIB}")
		set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${HYPERHEN_OUT_LIB}")
	else()
		set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
		set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
	endif()
endmacro()

# Define DEBUG for non-Release builds only.
#
# DEBUG used to be hardcoded into the flag string, so a --release build defined
# both DEBUG and NDEBUG. That left every #ifdef DEBUG branch — extra checks in
# libhijacker's containers, verbose logging — compiled into shipped payloads,
# and made "Release" mean nothing for them.
function(onion_target_debug_define target)
	target_compile_definitions(${target} PRIVATE $<$<NOT:$<CONFIG:Release>>:DEBUG>)
endfunction()

# Drop DWARF sections from a linked ELF. Keeps the on-device payload small;
# the unstripped object files stay in the build tree for symbolication.
function(onion_strip_debug target)
	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND ${CMAKE_OBJCOPY}
			--remove-section .debug_info --remove-section .debug_abbrev
			--remove-section .debug_line --remove-section .debug_str
			--remove-section .debug_loc --remove-section .debug_aranges
			--remove-section .debug_ranges --remove-section .debug_pubnames
			--remove-section .debug_pubtypes --remove-section .debug_frame
			--strip-unneeded $<TARGET_FILE:${target}>
		COMMENT "Stripping debugging information from ${target}"
	)
endfunction()
