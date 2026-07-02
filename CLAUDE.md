# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repository.

## What this repo is

The **DisplayXR vendor plug-in template** — a vendor-neutral, buildable starter
kit a new 3D-display vendor (BOE, Samsung, Acer, ...) clones to write a DisplayXR
display-processor plug-in. It implements the runtime's public plug-in ABI
(`xrtPluginNegotiate` + `xrt_plugin_iface` + a per-graphics-API DP vtable) with a
**stub weave** and **no proprietary SDK**. It is the forkable counterpart to two
things that aren't: the runtime's in-tree `sim_display` (not forkable) and the
Leia SR plug-in (needs Leia's SR SDK to build).

**Hard invariant: this must stay buildable by anyone.** Do NOT add a dependency
on any proprietary/vendor SDK, private package, or secret. The only external
dependency is the public DisplayXR runtime ABI, pulled via CMake `FetchContent`
(or a local checkout via `-DDXR_RUNTIME_SOURCE_DIR`). If a change would make the
template un-buildable on a clean machine, it's wrong for this repo.

## Layout

- `src/drv_example/example_plugin.c` — `xrtPluginNegotiate` + the `xrt_plugin_iface`
  vtable. The ABI entry point. Model: `sim_display_plugin.c` / `leia_plugin.c`.
- `src/drv_example/example_device.c` — the `xrt_device` (panel geometry + rendering
  modes). Model: `leia_device.c`.
- `src/drv_example/example_processor_d3d11.cpp` — D3D11 DP; stub column-interlace weave.
- `src/drv_example/example_processor_vk.c` — Vulkan DP (the `xrt_display_processor_vk`
  variant); stub passthrough-blit weave.
- `installer/` — NSIS stub that registers the plug-in in
  `HKLM\Software\DisplayXR\DisplayProcessors\example-vendor`.

## Vendor-adaptation workflow (what a fork changes)

1. Rename `drv_example` → `drv_<vendor>` and the `example_*` symbols.
2. Replace `process_atlas` in the DP files with the real weaver (the ONE
   mandatory change). Only `process_atlas` + `destroy` are mandatory DP slots;
   the other ~17/22 are optional and NULL-safe (`XRT_DP_HAS_SLOT`, ADR-020).
3. Make `example_plugin_probe` detect real hardware + decline cleanly when
   absent; drop the installer `ProbeOrder` from 200 (fallback) into 1–99.
4. Fill real geometry + eye-tracking capability in `example_plugin_get_display_info`
   and `example_device.c`.

## ABI source of truth

The plug-in ABI lives in the **runtime** repo (fetched into `build/_deps/`):

- `src/xrt/include/xrt/xrt_plugin.h` — `xrtPluginNegotiate`, `xrt_plugin_iface`,
  `xrt_plugin_display_info`, `XRT_PLUGIN_API_VERSION_CURRENT`.
- `src/xrt/include/xrt/xrt_display_processor.h` (+ `_d3d11.h`, `_vk.h`, ...) — the
  DP vtables. Each carries a `struct_size` header; appending a slot is
  compatible within a major, any other layout change is a major bump (ADR-020).

Rules when touching ABI-facing code:
- Set `iface.struct_size = sizeof(struct xrt_plugin_iface)` and each DP's
  `struct_size = sizeof(that vtable struct)` — the runtime uses these to tell
  which slots you built.
- `xrtPluginNegotiate` must return `XRT_PLUGIN_API_VERSION_CURRENT`; the loader
  rejects a mismatched major and falls back to `sim_display`.
- Keep `DXR_RUNTIME_GIT_TAG` (top-level `CMakeLists.txt`) pinned to a runtime
  release whose ABI major matches this build.

## Building

`scripts\build-windows.bat` (Windows), or `cmake -S . -B build -G Ninja && cmake
--build build` on any platform. First configure FetchContents the runtime (slow);
pass `-DDXR_RUNTIME_SOURCE_DIR=../displayxr-runtime` to use a local checkout.
Output: `DisplayXR-ExampleVendor.dll` in `build/**/`.

## Conventions

- License every new file `Apache-2.0` with a `The DisplayXR Project` copyright.
- Keep every stub honestly marked `// VENDOR TODO:`. Never present the stub weave
  as a real weave — it has no lens model, phase, or eye tracking.
- Match the runtime's code style (tabs, `snake_case`, `example_` prefix).
