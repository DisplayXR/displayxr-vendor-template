# DisplayXR Vendor Plug-in Template

**The neutral starting point for a new 3D-display vendor.**

This repository is a **buildable, ABI-correct starter kit** for writing a
[DisplayXR](https://github.com/DisplayXR/displayxr-runtime) display-processor
plug-in. Clone it, rename `drv_example` to your vendor, replace one function (the
weave), and you have a working plug-in.

> **This template builds with ZERO vendor SDK.** Nothing here depends on any
> proprietary library. `cmake` + a compiler + the public DisplayXR runtime ABI
> (pulled automatically via `FetchContent`) is all you need. That is the whole
> point: the runtime's in-tree neutral reference (`sim_display`) can't be forked,
> and the first real vendor plug-in (Leia SR) can't build without Leia's
> Simulated Reality SDK — so neither is a clean starting point. This is.

## What a DisplayXR plug-in is

DisplayXR is a vendor-neutral OpenXR runtime for 3D displays. The runtime does
everything except the vendor-specific bits: it renders each app's views into a
**tiled atlas texture**, then hands that atlas to your **display processor (DP)**,
which turns it into the panel's final output — the interlaced/woven lenticular
pattern your hardware needs. Your plug-in is a single DLL that:

1. exports one C symbol, `xrtPluginNegotiate`;
2. returns an `xrt_plugin_iface` vtable (probe hardware, create the display
   device, report display info);
3. provides a per-graphics-API DP factory whose `process_atlas` is **your weaver**.

The runtime discovers your DLL via the registry (Windows) or a JSON manifest
(macOS/Linux), loads it at `xrCreateInstance`, and drives it through those
vtables. See
[`docs/guides/vendor-plugin-onboarding.md`](https://github.com/DisplayXR/displayxr-runtime/blob/main/docs/guides/vendor-plugin-onboarding.md)
and the
[`xrt_plugin_iface` reference](https://github.com/DisplayXR/displayxr-runtime/blob/main/docs/reference/xrt_plugin_iface.md).

## What's in the box

```
displayxr-vendor-template/
├── CMakeLists.txt                    # FetchContent the runtime ABI; no vendor SDK
├── src/drv_example/
│   ├── example_interface.h           # internal glue declarations
│   ├── example_device.c              # the xrt_device (panel geometry + rendering modes)
│   ├── example_plugin.c              # xrtPluginNegotiate + the xrt_plugin_iface vtable
│   ├── example_processor_d3d11.cpp   # D3D11 DP — STUB column-interlace weave
│   ├── example_processor_vk.c        # Vulkan DP — STUB passthrough-blit weave
│   └── CMakeLists.txt
├── installer/
│   ├── DisplayXRExampleVendorInstaller.nsi   # registers the plug-in (STUB)
│   └── CMakeLists.txt
├── scripts/build-windows.bat         # convenience build wrapper
├── CLAUDE.md                         # guidance for AI-assisted work in this repo
└── LICENSE                           # Apache-2.0
```

Everything marked **STUB** is intentionally minimal but structurally real. The
D3D11 processor does a naive per-column interlace of the two atlas tiles; the
Vulkan processor blits the first tile through. Neither is a correct
autostereoscopic weave — they exist only to prove the pixel path end-to-end.
Every place real vendor code belongs is flagged `// VENDOR TODO:`.

## The 3-step adaptation flow

1. **Clone → rename `drv_example` to `drv_<yourvendor>`.** Rename the directory,
   the source files, and the `example_*` symbols to your own prefix. Change the
   iface `id` / `display_name` / `vendor` in `example_plugin.c` and the CMake
   target / installer id to match.
2. **Replace the stub weave with your weaver.** The only *mandatory* thing to
   change is the body of `process_atlas` in `example_processor_d3d11.cpp` /
   `example_processor_vk.c` — call your SDK's interlacer, or draw your own
   lenticular shader. Add `create_dp_d3d12` / `create_dp_gl` / `create_dp_metal`
   if you weave those APIs (NULL factories transparently fall back to
   `sim_display` for that API).
3. **Wire real hardware detection.** Make `example_plugin_probe` detect your
   panel (EDID / SDK / service handshake) and return
   `XRT_ERROR_PROBER_NOT_SUPPORTED` when it's absent, then drop the installer's
   `ProbeOrder` from `200` (fallback band) into `1–99` so you win over
   `sim_display` on machines with your hardware. Fill your real geometry +
   eye-tracking capability into `example_plugin_get_display_info` /
   `example_device.c`.

## Minimum viable DP: 2 slots of ~19

The DP vtable has ~19 (D3D11) / ~24 (Vulkan) slots, but **only two are
mandatory**: `process_atlas` and `destroy`. Every other slot is optional and
NULL-safe — the runtime guards each call with a `struct_size` + NULL check
(`XRT_DP_HAS_SLOT`, per ADR-020), so a plug-in can leave them all NULL and still
work. This template implements the two mandatory slots plus `is_alpha_native`,
and leaves the rest NULL with a `// VENDOR TODO:` noting what each is for
(eye-position prediction, hardware 2D/3D toggle, transparent background,
window-drag phase-snap, local 3D zones, ...). Add only the ones your product
needs.

**ABI major must match.** Your plug-in reports the ABI major it was built against
(`XRT_PLUGIN_API_VERSION_CURRENT`) from `xrtPluginNegotiate`. The runtime's loader
*rejects* any plug-in whose major differs and falls back to `sim_display`. Pin
`DXR_RUNTIME_GIT_TAG` in the top-level `CMakeLists.txt` to a runtime release whose
ABI major matches, and rebuild when you adopt a newer major.

## If your display weaves in hardware (FPGA / ASIC)

Not every 3D display expects final woven subpixels. If yours does the weave in
its own silicon — an FPGA or ASIC on the scaler board, fed an ordinary video
frame — your plug-in gets *simpler*, and this template ships that shape too:

```bat
set DXR_EXAMPLE_WEAVE_MODE=hardware     :: process_atlas becomes a passthrough
set DXR_EXAMPLE_WEAVE_SCOPE=region      :: canvas | region | scanout
```

**Why passthrough is the whole weave.** The compositor already lays the views
out as a `tile_columns x tile_rows` grid at panel size. Declare tile geometry
matching what your chip expects and the atlas you are handed **already is** the
packed frame — `2x1 @ 0.5,1.0` gives you left view in the left half, right view
in the right half, at exactly the target's dimensions. Side-by-side half is
`2x1`, top-and-bottom is `1x2`, an N-view quilt is any grid up to 8 views. There
is nothing to rearrange; a real plug-in adds only its signalling (a watermark
row stamped in `process_atlas`, or a sideband command from
`request_display_mode`).

**Declaring your weave scope is the one thing you must not skip.** The runtime
cannot infer how much of the panel your chip transforms, and it decides what
presentations can be correct:

| Scope | Your chip | Windowed apps |
|---|---|---|
| `CANVAS` | GPU weaver — final pixels for the canvas you were handed | native (and the default when the slot is NULL) |
| `REGION` | takes a "weave only this rect" descriptor | native — implement the zone slots to push the rect |
| `SCANOUT` | transforms the whole frame; no rect | need a panel-scoped (fullscreen) presentation |

See `example_dp_d3d11_get_scanout_caps`, and the runtime's
[vendor onboarding guide](https://github.com/DisplayXR/displayxr-runtime/blob/main/docs/guides/vendor-plugin-onboarding.md)
section *"Displays that weave in hardware (FPGA / ASIC)"* for the full contract
— including mode signalling, display timing, and what the runtime will not do
for you.

The slot is `#ifdef`-guarded on `XRT_DP_D3D11_HAS_SCANOUT_CAPS` so this template
keeps building against a `DXR_RUNTIME_GIT_TAG` that predates it; bump the pin to
turn it on.

## Building

```bat
:: Windows (from a VS 2022 x64 developer prompt, or via the wrapper)
scripts\build-windows.bat
```

or directly with CMake on any platform:

```bash
cmake -S . -B build -G Ninja        # first run FetchContents the runtime (slow)
cmake --build build
```

To iterate against a **local** runtime checkout (faster than re-fetching), pass
`-DDXR_RUNTIME_SOURCE_DIR=/path/to/displayxr-runtime`. The runtime's own build
prerequisites apply when built from source (vcpkg for Eigen3/cjson, the Vulkan
SDK, the OpenXR loader) — see the runtime repo's
[`docs/getting-started/building.md`](https://github.com/DisplayXR/displayxr-runtime/blob/main/docs/getting-started/building.md).

Output: `build/**/DisplayXR-ExampleVendor.dll`. Register it for a from-source
runtime with the runtime's `scripts\register_dev_plugin.bat`, then run any
`cube_*` test app and check the per-process log for
`active plug-in: id=example-vendor`.

## Android / POSIX note

- **POSIX (macOS/Linux):** the plug-in builds as a self-contained `.dylib`/`.so`
  (static-linked aux, one exported symbol) and is discovered via a JSON manifest
  under `~/Library/Application Support/DisplayXR/DisplayProcessors/` (macOS) or
  `$XDG_DATA_HOME/DisplayXR/DisplayProcessors/` (Linux), or any dir on
  `XRT_PLUGIN_SEARCH_PATH`. See
  [`docs/specs/runtime/plugin-discovery.md`](https://github.com/DisplayXR/displayxr-runtime/blob/main/docs/specs/runtime/plugin-discovery.md).
- **Android** is a supported DisplayXR target, but its display processor runs
  **out-of-process** in the runtime service and the DP delivers see-through
  differently (SurfaceFlinger composites the translucent surface). That model is
  described in **ADR-025**. This template leaves the Android CMake arm *stubbed*
  (it warns and skips) — port the Vulkan DP per ADR-025 before enabling it.

## License

Apache-2.0. This template is original DisplayXR code, deliberately permissive so
any vendor can build on it without friction. (The runtime itself is BSL-1.0 for
Monado-fork reasons; your plug-in links its public ABI and can carry whatever
license you choose.)
