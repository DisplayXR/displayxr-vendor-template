# Copyright 2026, The DisplayXR Project
# SPDX-License-Identifier: Apache-2.0
#
# The example vendor plug-in DLL. Builds against the runtime tree added by the
# top-level CMakeLists (local checkout or FetchContent). No vendor SDK.

if(ANDROID)
    # Android is a supported DisplayXR target, but the out-of-process display
    # processor model (ADR-025) + the NDK/CNSDK-style build differ enough that
    # this template leaves the Android arm STUBBED. See the README §Android note.
    message(WARNING
        "drv_example: Android build arm is stubbed in this template. "
        "Port the plug-in per ADR-025 (out-of-process DP) before enabling.")
    return()
endif()

# --- Sources -------------------------------------------------------------
# Always-present, cross-platform glue.
set(EXAMPLE_SOURCES
    example_interface.h
    example_device.c
    example_plugin.c
)

# Vulkan display processor — available whenever the runtime was built with VK
# (all desktop platforms; Android too, though that arm is stubbed above).
if(XRT_HAVE_VULKAN)
    list(APPEND EXAMPLE_SOURCES example_processor_vk.c)
    message(STATUS "drv_example: Vulkan display processor ENABLED")
else()
    message(STATUS "drv_example: Vulkan display processor DISABLED (XRT_HAVE_VULKAN off)")
endif()

# D3D11 display processor — Windows only.
if(WIN32)
    list(APPEND EXAMPLE_SOURCES example_processor_d3d11.cpp)
    message(STATUS "drv_example: D3D11 display processor ENABLED")
endif()

add_library(DisplayXR-ExampleVendor SHARED ${EXAMPLE_SOURCES})

target_include_directories(DisplayXR-ExampleVendor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# --- Link against the runtime ABI ---------------------------------------
if(WIN32)
    # ADR-019 Option-C boundary: define XRT_USING_RUNTIME_DLL so the aux headers'
    # XRT_API_FUNC symbols resolve to DisplayXRClient.dll (one log sink / u_var
    # registry per process), and link its import lib. The non-exported aux
    # helpers are pulled in per-.obj from the static aux libs.
    target_compile_definitions(DisplayXR-ExampleVendor PRIVATE XRT_USING_RUNTIME_DLL)
    target_link_libraries(DisplayXR-ExampleVendor PRIVATE
        # DisplayXRClient.lib — the runtime DLL's import library. Generator
        # expression avoids dragging in the runtime target's PUBLIC interface
        # link libs (every compositor, the state tracker, ...).
        $<TARGET_LINKER_FILE:${RUNTIME_TARGET}>
        # Non-exported aux helpers (device builder, math, containers).
        xrt-interfaces
        aux_util
        aux_os
        aux_math
        drv_includes
    )
    add_dependencies(DisplayXR-ExampleVendor ${RUNTIME_TARGET})

    # Graphics APIs this plug-in weaves.
    target_link_libraries(DisplayXR-ExampleVendor PRIVATE d3d11 d3dcompiler dxgi)
else()
    # POSIX desktop (Linux/macOS): self-contained plug-in — STATIC-link aux and
    # do NOT link the runtime .so. The version script / hidden visibility below
    # keeps xrtPluginNegotiate the only exported symbol, so the statically-linked
    # aux copies can't collide with the runtime's (same net effect as the Windows
    # import-lib boundary, no import lib needed). Mirrors the runtime's own
    # self-contained sim_display plug-in on ELF/Mach-O.
    target_link_libraries(DisplayXR-ExampleVendor PRIVATE
        xrt-interfaces
        aux_util
        aux_os
        aux_math
        drv_includes
    )
endif()

# Vulkan aux helpers (vk/vk_helpers.h — the vk_bundle dispatch table).
if(XRT_HAVE_VULKAN)
    target_link_libraries(DisplayXR-ExampleVendor PRIVATE aux_vk)
    target_include_directories(DisplayXR-ExampleVendor PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()

# --- One-symbol export discipline ---------------------------------------
# Only xrtPluginNegotiate (decorated XRT_PLUGIN_EXPORT) is visible.
set_target_properties(DisplayXR-ExampleVendor PROPERTIES
    OUTPUT_NAME "DisplayXR-ExampleVendor"
    PREFIX ""
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# --- Install ------------------------------------------------------------
# Drops the DLL under <prefix>/bin/plugins/ — the installer picks it up there.
install(
    TARGETS DisplayXR-ExampleVendor
    RUNTIME DESTINATION bin/plugins
    LIBRARY DESTINATION bin/plugins
)
