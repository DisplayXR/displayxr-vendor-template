// Copyright 2026, The DisplayXR Project
// SPDX-License-Identifier: Apache-2.0
/*!
 * @file
 * @brief  Internal interface for the example 3D-display vendor plug-in.
 *
 * This is the vendor-neutral STARTER KIT. Everything here is glue that a real
 * 3D-display vendor (BOE, Samsung, Acer, ...) keeps; the only thing you replace
 * is the body of the display-processor `process_atlas` (the weave) and the
 * hardware detection in `probe()`. There is NO proprietary SDK behind any of
 * this — it builds and runs on any machine.
 *
 * Rename `drv_example` → `drv_<yourvendor>` and the `example_*` symbols to your
 * own prefix when you fork this template.
 *
 * @ingroup drv_example
 */

#pragma once

#include "xrt/xrt_results.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct xrt_device;
struct xrt_display_processor;
struct xrt_display_processor_d3d11;

/*!
 * Create the example vendor's head/display xrt_device.
 *
 * Modelled on the sim_display / Leia device builders in the runtime tree. It
 * advertises two rendering modes — a 2D mono mode (1 view, 1x1 tile) and a 3D
 * stereo mode (2 views, 2x1 side-by-side tiles) — which is exactly the atlas
 * shape the example D3D11 / Vulkan weavers below consume.
 *
 * VENDOR TODO: query your real panel geometry / refresh / eye-box from your SDK
 * and fill the rendering modes accordingly. Add N-view lenticular modes here if
 * your display supports them.
 *
 * @return A new xrt_device, or NULL on failure.
 */
struct xrt_device *
example_hmd_create(void);

/*!
 * Bind an external pose source (the runtime's qwerty WASD/mouse HMD) to the
 * device so head-tracked camera movement works without real eye tracking.
 * Passing NULL clears the binding. Routed through the plug-in iface's
 * `set_pose_source` so the runtime never casts the vendor device itself.
 */
void
example_hmd_set_pose_source(struct xrt_device *xdev, struct xrt_device *source);

/*!
 * Fill the vendor-neutral display info the runtime reports to apps.
 * Returns true if populated. Called from the plug-in's `get_display_info`.
 */
bool
example_hmd_get_display_info(struct xrt_device *xdev,
                             float *out_width_m,
                             float *out_height_m,
                             float *out_nominal_z_m,
                             uint32_t *out_pixel_width,
                             uint32_t *out_pixel_height);

#if defined(_WIN32)
/*!
 * D3D11 display-processor factory. Matches @ref xrt_dp_factory_d3d11_fn_t.
 * The compositor calls this at session creation for a D3D11 app.
 */
xrt_result_t
example_dp_factory_d3d11(void *d3d11_device,
                         void *d3d11_context,
                         void *window_handle,
                         struct xrt_display_processor_d3d11 **out_xdp);
#endif

/*!
 * Vulkan display-processor factory. Matches @ref xrt_dp_factory_vk_fn_t.
 * Available whenever the runtime was built with Vulkan (all desktop platforms +
 * Android). The compositor calls this at session creation for a Vulkan app.
 */
xrt_result_t
example_dp_factory_vk(void *vk_bundle,
                      void *vk_cmd_pool,
                      void *window_handle,
                      int32_t target_format,
                      struct xrt_display_processor **out_xdp);

#ifdef __cplusplus
}
#endif
