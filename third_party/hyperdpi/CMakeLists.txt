cmake_minimum_required(VERSION 3.20)

project(HyperHENHyperDPI VERSION 1.0.0 LANGUAGES C)

set(HYPERDPI_TARGET hyperdpi)
set(HYPERDPI_PLUGIN_ID HYPD00001)
set(HYPERDPI_PLUGIN_VERSION 1.00)
set(HYPERDPI_PLUGIN_NAME "HyperDPI")

if(NOT TARGET HyperHEN::Runtime)
    message(FATAL_ERROR
        "HyperDPI requires the vendored HyperHEN plugin runtime")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/hyperdpi_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/hyperdpi_config.h"
    @ONLY)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB HYPERDPI_LOCALE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/i18n/*.json")
set(HYPERDPI_I18N_CATALOG
    "${CMAKE_CURRENT_BINARY_DIR}/generated/hyperdpi_i18n_catalog.inc")
add_custom_command(
    OUTPUT "${HYPERDPI_I18N_CATALOG}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_notification_catalog.py"
        --locales-dir "${CMAKE_CURRENT_SOURCE_DIR}/i18n"
        --output "${HYPERDPI_I18N_CATALOG}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_notification_catalog.py"
        ${HYPERDPI_LOCALE_FILES}
    VERBATIM)

set(HYPERDPI_PKGSERVER_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pkgserver")
set(HYPERDPI_PKGSERVER_SOURCES
    "${HYPERDPI_PKGSERVER_DIR}/pkg_server.c"
    "${HYPERDPI_PKGSERVER_DIR}/multipart.c")
set(HYPERDPI_WEBUI_INDEX
    "${CMAKE_CURRENT_SOURCE_DIR}/webui/dist/index.html")
if(NOT EXISTS "${HYPERDPI_WEBUI_INDEX}")
    message(FATAL_ERROR
        "HyperDPI WebUI bundle missing: ${HYPERDPI_WEBUI_INDEX}")
endif()

onion_add_plugin(
    NAME ${HYPERDPI_TARGET}
    TITLE_ID ${HYPERDPI_PLUGIN_ID}
    VERSION ${HYPERDPI_PLUGIN_VERSION}
    SOURCES
        source/main.c
        source/hyperdpi_i18n.c
        source/hyperdpi_service.c
        source/hyperdpi_descriptor.c
        source/hyperdpi_settings.c
        source/hyperdpi_ui.c
        ${HYPERDPI_PKGSERVER_SOURCES}
        "${HYPERDPI_I18N_CATALOG}"
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${HYPERDPI_PKGSERVER_DIR}"
        "${HYPERDPI_PKGSERVER_DIR}/include"
    LIBRARIES
        SceLibcInternal
        SceSystemService
        SceAppInstUtil
        SceNotification
        SceNetCtl
        SceNet
        kernel_sys)

set_source_files_properties(
    "${HYPERDPI_PKGSERVER_DIR}/pkg_server.c"
    PROPERTIES
        COMPILE_DEFINITIONS
            "HYPERHEN_HYPERDPI_WEBUI_INDEX=\"${HYPERDPI_WEBUI_INDEX}\""
        OBJECT_DEPENDS "${HYPERDPI_WEBUI_INDEX}")
set_source_files_properties(${HYPERDPI_PKGSERVER_SOURCES} PROPERTIES
    COMPILE_FLAGS
        "-Wno-error -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare")
