// Copyright 2026, The DisplayXR Project
// SPDX-License-Identifier: Apache-2.0
/*!
 * @file
 * @brief  Plug-in entry point for the example 3D-display vendor.
 *
 * Implements @ref xrt_plugin_negotiate_fn_t (xrt/xrt_plugin.h). The runtime DLL
 * discovers this plug-in via registry (Windows) / JSON-manifest (POSIX), loads
 * it, resolves the single exported symbol `xrtPluginNegotiate`, and dispatches
 * through the returned @ref xrt_plugin_iface vtable.
 *
 * This is the vendor-neutral starting point. Everything here is real,
 * ABI-correct glue; the ONLY things a vendor must change to ship a working
 * plug-in are:
 *   1. `example_plugin_probe`  — detect YOUR hardware, decline cleanly when absent.
 *   2. `example_dp_*_process_atlas` — YOUR weave (see example_processor_*).
 *   3. `example_plugin_get_display_info` — YOUR panel geometry / eye-tracking.
 *
 * @ingroup drv_example
 */

// XRT_HAVE_* feature macros (XRT_HAVE_VULKAN etc). Include this FIRST — without
// it the `#if defined(XRT_HAVE_VULKAN)` gates below are silently false and the
// VK factory never gets wired even when the VK processor is compiled in. (This
// was a real bug in the runtime's own sim_display plug-in, #456.)
#include "xrt/xrt_config_have.h"
#include "xrt/xrt_config_os.h"

#include "xrt/xrt_plugin.h"
#include "xrt/xrt_results.h"

#include "util/u_logging.h"

#include "example_interface.h"

#include <stddef.h>


/*
 *
 * Vtable callbacks.
 *
 */

static xrt_result_t
example_plugin_probe(struct xrt_plugin_instance **out_inst)
{
	/*
	 * VENDOR TODO: detect YOUR display here.
	 *
	 * A real hardware plug-in checks for its panel (EDID match, an SDK /
	 * service handshake, a USB probe, ...) and:
	 *   - returns XRT_SUCCESS  → this plug-in claims the system, and
	 *   - returns XRT_ERROR_PROBER_NOT_SUPPORTED → declines CLEANLY so the
	 *     runtime moves on to the next plug-in (and ultimately sim_display).
	 *
	 * probe() is on the xrCreateInstance hot path — keep it sub-millisecond
	 * (cache any slow SDK call in a file-scope static).
	 *
	 * This template ALWAYS claims the system (like the runtime's sim_display
	 * fallback) so it runs out-of-the-box with no hardware. Register it with
	 * a high ProbeOrder (200 range) so it only wins when no real vendor
	 * plug-in claimed the machine — see installer/DisplayXRExampleVendorInstaller.nsi.
	 * When you wire real detection, drop ProbeOrder into the 1–99 range.
	 *
	 * There is no per-instance state: the device is a process singleton, so we
	 * hand back a NULL handle (the runtime passes it back to every call).
	 */
	*out_inst = NULL;
	return XRT_SUCCESS;
}

static xrt_result_t
example_plugin_create_device(struct xrt_plugin_instance *inst, struct xrt_device **out_dev)
{
	(void)inst;
	struct xrt_device *xdev = example_hmd_create();
	if (xdev == NULL) {
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}
	*out_dev = xdev;
	return XRT_SUCCESS;
}

static void
example_plugin_destroy(struct xrt_plugin_instance *inst)
{
	(void)inst;
	/* No per-instance state to free. */
}

static void
example_plugin_set_pose_source(struct xrt_plugin_instance *inst,
                               struct xrt_device *xdev,
                               struct xrt_device *source)
{
	(void)inst;
	example_hmd_set_pose_source(xdev, source);
}

static bool
example_plugin_get_display_info(struct xrt_plugin_instance *inst,
                                struct xrt_device *xdev,
                                struct xrt_plugin_display_info *out_info)
{
	(void)inst;

	/*
	 * Forward-compat: the runtime sets out_info->struct_size to its own
	 * sizeof(struct xrt_plugin_display_info) before this call. Runtime and
	 * plug-in built against the same header ⟹ equal, so we can write every
	 * field. If you build against an OLDER header than the runtime, gate any
	 * newer field on `struct_size` before writing it. (See xrt_plugin.h.)
	 */
	(void)out_info->struct_size;

	float w_m = 0.0f, h_m = 0.0f, z_m = 0.0f;
	uint32_t px_w = 0, px_h = 0;
	if (!example_hmd_get_display_info(xdev, &w_m, &h_m, &z_m, &px_w, &px_h)) {
		return false;
	}

	out_info->display_width_m = w_m;
	out_info->display_height_m = h_m;
	out_info->nominal_viewer_x_m = 0.0f;
	out_info->nominal_viewer_y_m = 0.0f;
	out_info->nominal_viewer_z_m = z_m;
	out_info->display_pixel_width = px_w;
	out_info->display_pixel_height = px_h;

	// VENDOR TODO: report your SDK's recommended per-view render scale
	// (recommended_view_scale_x/y). 0 ⟹ the runtime derives one from the
	// worst-case rendering mode's tile layout, which is fine for this template.
	out_info->recommended_view_scale_x = 0.0f;
	out_info->recommended_view_scale_y = 0.0f;

	// VENDOR TODO: report the display's top-left in virtual-screen coordinates
	// (from EDID) so the workspace can position windows over your panel.
	out_info->display_screen_left = 0;
	out_info->display_screen_top = 0;

	// VENDOR TODO: eye-tracking capability bitmask.
	//   bit 0 (0x1) = MANAGED (your SDK predicts eye positions each frame)
	//   bit 1 (0x2) = MANUAL  (the app submits eye positions)
	//   0           = no eye tracker (this template — positions are nominal).
	// If you set a non-zero mask here, you MUST also set
	// XRT_RENDERING_MODE_FLAG_HAS_TRACKING on at least one rendering mode in
	// example_device.c (the runtime asserts the two agree — #441).
	out_info->supported_eye_tracking_modes = 0u;
	out_info->default_eye_tracking_mode = 0u;

	return true;
}

static uint32_t
example_plugin_probe_displays(struct xrt_plugin_instance *inst,
                              const struct xrt_display_descriptor *displays,
                              uint32_t display_count,
                              struct xrt_display_claim *out_claims,
                              uint32_t max_claims)
{
	(void)inst;

	/*
	 * Per-monitor claims (ADR-015). OPTIONAL — a plug-in whose iface
	 * struct_size predates this slot, or that leaves it NULL, falls back to a
	 * single implicit claim from the binary probe(). This template mirrors the
	 * fallback shape: claim EVERY monitor at FALLBACK confidence.
	 *
	 * VENDOR TODO: match each descriptor's EDID (edid_manufacturer/product)
	 * against YOUR known-panel table and claim only your monitors, at
	 * XRT_DISPLAY_CLAIM_EDID or _VERIFIED confidence (both outrank FALLBACK).
	 */
	uint32_t n = 0;
	for (uint32_t i = 0; i < display_count && n < max_claims; i++) {
		struct xrt_display_claim *c = &out_claims[n++];
		c->monitor_id = displays[i].monitor_id;
		c->confidence = (uint32_t)XRT_DISPLAY_CLAIM_FALLBACK;

		// Mirror the #ifdef gating of the DP factory fields below.
		c->supported_apis = 0;
#if defined(_WIN32)
		c->supported_apis |= XRT_DP_API_BIT_D3D11;
#endif
#if defined(XRT_HAVE_VULKAN)
		c->supported_apis |= XRT_DP_API_BIT_VK;
#endif
		c->serial[0] = '\0';
	}
	return n;
}


/*
 *
 * Vtable.
 *
 */

static struct xrt_plugin_iface g_example_iface = {
    .struct_size = sizeof(struct xrt_plugin_iface),
    .reserved_0 = 0,

    // VENDOR TODO: your kebab-case id (must match the installer registry key /
    // JSON manifest id), display name, publisher, and version.
    .id = "example-vendor",
    .display_name = "DisplayXR Example Vendor",
    .vendor = "The DisplayXR Project",
    .version = "0.1.0",

    .probe = example_plugin_probe,
    .create_device = example_plugin_create_device,

    /*
     * Per-graphics-API DP factories. NULL ⟹ "this API isn't supported by this
     * plug-in on this platform" — the runtime then transparently falls back to
     * the sim_display DP for that API path on the same probe-winning device, so
     * you only need to implement the APIs your weaver actually supports.
     *
     * This template ships D3D11 (Windows) + Vulkan (all platforms with VK).
     * VENDOR TODO: add create_dp_d3d12 / create_dp_gl / create_dp_metal if you
     * weave those APIs.
     */
#if defined(_WIN32)
    .create_dp_d3d11 = example_dp_factory_d3d11,
#else
    .create_dp_d3d11 = NULL,
#endif

#if defined(XRT_HAVE_VULKAN)
    .create_dp_vk = example_dp_factory_vk,
#else
    .create_dp_vk = NULL,
#endif

    .create_dp_d3d12 = NULL,
    .create_dp_gl = NULL,
    .create_dp_metal = NULL,

    .destroy = example_plugin_destroy,
    .get_display_info = example_plugin_get_display_info,
    .set_pose_source = example_plugin_set_pose_source,
    .probe_displays = example_plugin_probe_displays,
};


/*
 *
 * Entry point — the single exported symbol.
 *
 */

XRT_PLUGIN_EXPORT xrt_result_t
xrtPluginNegotiate(uint32_t runtime_api_version,
                   const struct xrt_plugin_host_iface *host,
                   struct xrt_plugin_iface **out_iface,
                   uint32_t *out_plugin_api_version)
{
	// The host iface carries Android JavaVM/Activity accessors and reserved
	// slots; unused by this desktop-first template. NULL-check before use.
	(void)host;

	// Report the ABI major we were built against. The runtime's loader REJECTS
	// any plug-in whose major differs from its own (ADR-020 rule 3) and falls
	// back to the next plug-in / sim_display. Keep your CMake runtime pin on a
	// tag that ships the same XRT_PLUGIN_API_VERSION_CURRENT.
	*out_plugin_api_version = XRT_PLUGIN_API_VERSION_CURRENT;

	if (runtime_api_version != XRT_PLUGIN_API_VERSION_CURRENT) {
		*out_iface = NULL;
		return XRT_ERROR_PROBER_NOT_SUPPORTED;
	}

	*out_iface = &g_example_iface;
	return XRT_SUCCESS;
}
