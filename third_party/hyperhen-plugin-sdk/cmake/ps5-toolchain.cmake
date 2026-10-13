set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT PS5_PAYLOAD_SDK)
    if(DEFINED ENV{PS5_PAYLOAD_SDK})
        set(PS5_PAYLOAD_SDK "$ENV{PS5_PAYLOAD_SDK}")
    else()
        message(FATAL_ERROR "Set PS5_PAYLOAD_SDK to a ps5-payload-sdk checkout")
    endif()
endif()

set(_prospero_clang "${PS5_PAYLOAD_SDK}/bin/prospero-clang")
set(_prospero_clangxx "${PS5_PAYLOAD_SDK}/bin/prospero-clang++")
if(NOT EXISTS "${_prospero_clang}" OR NOT EXISTS "${_prospero_clangxx}")
    message(FATAL_ERROR "Prospero clang binaries not found under ${PS5_PAYLOAD_SDK}/bin")
endif()

set(CMAKE_C_COMPILER "${_prospero_clang}")
set(CMAKE_CXX_COMPILER "${_prospero_clangxx}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER_TARGET x86_64-sie-ps5)
set(CMAKE_CXX_COMPILER_TARGET x86_64-sie-ps5)

set(CMAKE_FIND_ROOT_PATH "${PS5_PAYLOAD_SDK}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

