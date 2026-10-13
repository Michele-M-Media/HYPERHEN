cmake_minimum_required(VERSION 3.20)

project(HyperHENPluginSDK VERSION 0.1.0 LANGUAGES C)

option(ONION_SDK_BUILD_SAMPLES "Build the PS5 plugin samples" OFF)
option(ONION_SDK_BUILD_TESTS "Build host-side SDK tests" OFF)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(Threads REQUIRED)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_library(onion_sdk_headers INTERFACE)
add_library(HyperHEN::SDK ALIAS onion_sdk_headers)
set_target_properties(onion_sdk_headers PROPERTIES EXPORT_NAME SDK)
target_include_directories(onion_sdk_headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

include(OnionPlugin)
add_subdirectory(runtime)

if(ONION_SDK_BUILD_TESTS)
    enable_testing()
    add_executable(sdk_runtime_test tests/test_runtime.c)
    target_link_libraries(sdk_runtime_test PRIVATE HyperHEN::Runtime Threads::Threads)
    target_compile_features(sdk_runtime_test PRIVATE c_std_11)
    target_compile_options(sdk_runtime_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME sdk_runtime COMMAND sdk_runtime_test)
    add_executable(sdk_ui_test tests/test_ui.c)
    target_link_libraries(sdk_ui_test PRIVATE HyperHEN::Runtime Threads::Threads)
    target_compile_features(sdk_ui_test PRIVATE c_std_11)
    target_compile_options(sdk_ui_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME sdk_ui COMMAND sdk_ui_test)
    add_test(NAME sdk_python_tools
        COMMAND ${Python3_EXECUTABLE} -c
            "import importlib.util; s=importlib.util.spec_from_file_location('t','${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tools.py'); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); m.test_inspect_and_deploy()")
endif()

if(ONION_SDK_BUILD_SAMPLES)
    add_subdirectory(samples/hello)
    add_subdirectory(samples/daemon)
    add_subdirectory(samples/dynamic_ui)
endif()

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES cmake/OnionPlugin.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HyperHENPluginSDK)
install(PROGRAMS
    tools/inspect_plugin.py
    tools/deploy_plugin.py
    DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/HyperHENPluginSDKConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/HyperHENPluginSDKConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HyperHENPluginSDK)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/HyperHENPluginSDKConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion)
install(EXPORT HyperHENPluginSDKTargets
    FILE HyperHENPluginSDKTargets.cmake
    NAMESPACE HyperHEN::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HyperHENPluginSDK)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/HyperHENPluginSDKConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/HyperHENPluginSDKConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HyperHENPluginSDK)
install(TARGETS onion_sdk_headers onion_sdk_runtime EXPORT HyperHENPluginSDKTargets)
