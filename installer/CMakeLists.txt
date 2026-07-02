# Copyright 2026, The DisplayXR Project
# SPDX-License-Identifier: Apache-2.0
#
# NSIS installer for the example vendor plug-in (Windows). Minimal STUB — it
# registers the plug-in so the runtime's registry discovery finds it. A real
# vendor lifts this wholesale and renames the id / install dir / strings.

if(NOT WIN32)
    return()
endif()

find_program(NSIS_EXECUTABLE makensis
    PATHS
        "$ENV{PROGRAMFILES}/NSIS"
        "$ENV{PROGRAMFILES\(X86\)}/NSIS"
        "C:/Program Files/NSIS"
        "C:/Program Files (x86)/NSIS"
)

if(NOT NSIS_EXECUTABLE)
    message(WARNING "NSIS not found — the 'installer' target will not be available. "
                    "Install NSIS (https://nsis.sourceforge.io) to build the plug-in installer.")
    return()
endif()

message(STATUS "Found NSIS: ${NSIS_EXECUTABLE}")

set(EXAMPLE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(EXAMPLE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(EXAMPLE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(EXAMPLE_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

# The DLL lands in <install-prefix>/bin/plugins/ (see src/drv_example install()).
set(EXAMPLE_BIN_DIR "${CMAKE_INSTALL_PREFIX}/bin")

# Optional code-signing command from the SIGN_CMD env var (empty = unsigned).
# No cert/secret lives in this repo — mirrors the runtime + Leia plug-in.
set(SIGN_CMD "$ENV{SIGN_CMD}" CACHE STRING "Code-signing command for installer/uninstaller (empty = unsigned)")

add_custom_target(installer
    COMMAND "${NSIS_EXECUTABLE}"
        "/DVERSION=${EXAMPLE_VERSION}"
        "/DVERSION_MAJOR=${EXAMPLE_VERSION_MAJOR}"
        "/DVERSION_MINOR=${EXAMPLE_VERSION_MINOR}"
        "/DVERSION_PATCH=${EXAMPLE_VERSION_PATCH}"
        "/DBUILD_NUM=${BUILD_NUM}"
        "/DBIN_DIR=${EXAMPLE_BIN_DIR}"
        "/DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "/DOUTPUT_DIR=${CMAKE_INSTALL_PREFIX}"
        "/DSIGN_CMD=${SIGN_CMD}"
        "${CMAKE_CURRENT_SOURCE_DIR}/DisplayXRExampleVendorInstaller.nsi"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building DisplayXR Example Vendor plug-in installer..."
    VERBATIM
)
add_dependencies(installer DisplayXR-ExampleVendor)
