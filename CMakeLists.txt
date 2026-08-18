# Copyright 2026, The DisplayXR Project
# SPDX-License-Identifier: Apache-2.0
#
# DisplayXR vendor plug-in TEMPLATE — vendor-neutral starter kit.
#
# Builds DisplayXR-ExampleVendor.dll (Windows: D3D11 + Vulkan). It consumes ONLY
# the DisplayXR runtime's public ABI (headers + import lib), pulled in via CMake
# FetchContent — there is NO proprietary vendor SDK anywhere in this build. Fork
# it, rename drv_example -> drv_<yourvendor>, and replace the stub weave.

cmake_minimum_required(VERSION 3.22)

project(
    DisplayXR-ExampleVendor
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "DisplayXR example vendor display-processor plug-in (template)"
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED BUILD_NUM)
    set(BUILD_NUM "0")
endif()

# --------------------------------------------------------------------------
# Runtime dependency.
#
# The plug-in DLL consumes the runtime's public ABI (xrt/xrt_plugin.h,
# xrt/xrt_display_processor*.h, aux/util/*, aux/vk/*, ...) and, on Windows,
# links DisplayXRClient.lib (the runtime DLL's import lib) for the exported aux
# surface (ADR-019).
#
# Two modes:
#   - DXR_RUNTIME_SOURCE_DIR set to a local displayxr-runtime checkout: fast dev
#     iteration (and it inherits that tree's vcpkg toolchain — see below).
#   - Otherwise FetchContent fetches DXR_RUNTIME_GIT_TAG from GitHub.
#
# The RUNTIME_TARGET variable (the runtime DLL's CMake target name) is set by the
# runtime's own CMake as a CACHE INTERNAL, so it is visible here after the tree
# is added — the plug-in target links $<TARGET_LINKER_FILE:${RUNTIME_TARGET}>.
# --------------------------------------------------------------------------

if(DXR_RUNTIME_SOURCE_DIR)
    message(STATUS "Using local runtime checkout: ${DXR_RUNTIME_SOURCE_DIR}")
    add_subdirectory("${DXR_RUNTIME_SOURCE_DIR}" runtime-build EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    # Pinned to a displayxr-runtime release tag whose XRT_PLUGIN_API_VERSION_CURRENT
    # matches what this plug-in is built against. The runtime's loader REJECTS any
    # plug-in reporting a different ABI major (ADR-020 rule 3) and silently falls
    # back to sim_display.
    # VENDOR TODO: bump this tag + rebuild whenever you adopt a newer ABI major.
    #
    # This value MUST equal RUNTIME_REF in .github/workflows/build-windows.yml:
    # the "Rule-5 ABI pin self-check" step there regexes this line and hard-fails
    # the build on drift, so keep the literal `set(DXR_RUNTIME_GIT_TAG "vX.Y.Z"`
    # shape (no computed value, no variable indirection).
    #
    # There is no separate ABI-floor constant to keep in step: the plug-in reports
    # XRT_PLUGIN_API_VERSION_CURRENT from the headers of whichever runtime this tag
    # fetches (src/drv_example/example_plugin.c), so bumping the tag re-pairs the
    # ABI automatically.
    set(DXR_RUNTIME_GIT_TAG "v2.7.1" CACHE STRING
        "displayxr-runtime git ref to fetch (pin to a release tag for reproducible builds).")
    message(STATUS "Fetching displayxr-runtime via FetchContent @ ${DXR_RUNTIME_GIT_TAG}")
    FetchContent_Declare(
        displayxr_runtime
        GIT_REPOSITORY https://github.com/DisplayXR/displayxr-runtime.git
        GIT_TAG "${DXR_RUNTIME_GIT_TAG}"
    )
    FetchContent_MakeAvailable(displayxr_runtime)
endif()

# --------------------------------------------------------------------------
# The plug-in + installer.
# --------------------------------------------------------------------------
add_subdirectory(src/drv_example)

# NSIS installer is Windows-only (registers the plug-in in the registry). On
# macOS/Linux a vendor writes a JSON manifest instead — see the README §Android
# / POSIX note and docs/specs/runtime/plugin-discovery.md in the runtime repo.
if(WIN32)
    add_subdirectory(installer)
endif()
