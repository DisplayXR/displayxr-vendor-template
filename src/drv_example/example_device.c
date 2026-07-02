// Copyright 2026, The DisplayXR Project
// SPDX-License-Identifier: Apache-2.0
/*!
 * @file
 * @brief  Example 3D-display xrt_device (vendor-neutral, no SDK).
 *
 * Creates the head/display device the runtime prober + system-builder need.
 * Structurally identical to the runtime's `leia_device.c` / `sim_display_device.c`
 * builders, but with all geometry hard-coded to plausible defaults instead of
 * queried from a vendor SDK.
 *
 * VENDOR TODO markers below flag every place a real vendor swaps a default for
 * a value from its own hardware/SDK.
 *
 * @ingroup drv_example
 */

#include "example_interface.h"

#include "xrt/xrt_device.h"

#include "math/m_api.h"
#include "math/m_mathinclude.h"

#include "util/u_device.h"
#include "util/u_distortion_mesh.h"
#include "util/u_logging.h"
#include "util/u_var.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*!
 * Example 3D display device.
 * @implements xrt_device
 * @ingroup drv_example
 */
struct example_hmd
{
	struct xrt_device base;

	//! Stationary pose (viewer at nominal distance in front of the display).
	struct xrt_pose pose;

	//! Optional external device supplying pose (the runtime's qwerty HMD).
	//! When set, get_tracked_pose delegates to it. NULL ⟹ static pose.
	struct xrt_device *pose_source;

	//! Physical display dimensions in meters.
	float display_width_m;
	float display_height_m;

	//! Native panel resolution in pixels.
	uint32_t display_pixel_width;
	uint32_t display_pixel_height;

	//! Nominal viewer distance in meters.
	float nominal_z_m;

	enum u_logging_level log_level;
};

static inline struct example_hmd *
example_hmd(struct xrt_device *xdev)
{
	return (struct example_hmd *)xdev;
}


/*
 *
 * xrt_device interface methods.
 *
 */

static xrt_result_t
example_hmd_get_tracked_pose(struct xrt_device *xdev,
                             enum xrt_input_name name,
                             int64_t at_timestamp_ns,
                             struct xrt_space_relation *out_relation)
{
	struct example_hmd *hmd = example_hmd(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		U_LOG_E("Unknown input name: 0x%08x", name);
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	// Delegate to the external pose source (qwerty WASD/mouse) when bound.
	// VENDOR TODO: if your display has real head/eye tracking, feed the
	// tracked head pose in here instead (or in addition, e.g. for a
	// MANAGED eye-tracking product).
	if (hmd->pose_source != NULL) {
		struct xrt_space_relation src_rel;
		hmd->pose_source->get_tracked_pose(hmd->pose_source, name, at_timestamp_ns, &src_rel);
		out_relation->pose = src_rel.pose;
		out_relation->relation_flags = (enum xrt_space_relation_flags)(
		    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
		    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);
		return XRT_SUCCESS;
	}

	// Static pose: viewer at the nominal position in front of the display.
	out_relation->pose = hmd->pose;
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);
	return XRT_SUCCESS;
}

static void
example_hmd_destroy(struct xrt_device *xdev)
{
	struct example_hmd *hmd = example_hmd(xdev);
	u_var_remove_root(hmd);
	u_device_free(&hmd->base);
}

void
example_hmd_set_pose_source(struct xrt_device *xdev, struct xrt_device *source)
{
	struct example_hmd *hmd = example_hmd(xdev);
	hmd->pose_source = source;
}

bool
example_hmd_get_display_info(struct xrt_device *xdev,
                             float *out_width_m,
                             float *out_height_m,
                             float *out_nominal_z_m,
                             uint32_t *out_pixel_width,
                             uint32_t *out_pixel_height)
{
	if (xdev == NULL) {
		return false;
	}
	// Identify our own device (the runtime may hold several).
	if (strcmp(xdev->serial, "example_display_0") != 0) {
		return false;
	}
	struct example_hmd *hmd = example_hmd(xdev);
	if (out_width_m) {
		*out_width_m = hmd->display_width_m;
	}
	if (out_height_m) {
		*out_height_m = hmd->display_height_m;
	}
	if (out_nominal_z_m) {
		*out_nominal_z_m = hmd->nominal_z_m;
	}
	if (out_pixel_width) {
		*out_pixel_width = hmd->display_pixel_width;
	}
	if (out_pixel_height) {
		*out_pixel_height = hmd->display_pixel_height;
	}
	return true;
}


/*
 *
 * Creation function.
 *
 */

struct xrt_device *
example_hmd_create(void)
{
	// VENDOR TODO: replace these hard-coded defaults with values queried
	// from your display / SDK (EDID, a service handshake, a calibration
	// blob, ...). These are plausible 15.6" 4K-ish light-field defaults.
	const int pixel_w = 3840;
	const int pixel_h = 2160;
	const float display_w_m = 0.344f; // ~34.4 cm
	const float display_h_m = 0.194f; // ~19.4 cm
	const float nominal_z = 0.65f;    // ~65 cm viewing distance
	const float ipd_m = 0.063f;       // ~63 mm inter-pupillary distance

	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	struct example_hmd *hmd = U_DEVICE_ALLOCATE(struct example_hmd, flags, 1, 0);
	if (hmd == NULL) {
		return NULL;
	}

	// Store config.
	hmd->display_width_m = display_w_m;
	hmd->display_height_m = display_h_m;
	hmd->display_pixel_width = (uint32_t)pixel_w;
	hmd->display_pixel_height = (uint32_t)pixel_h;
	hmd->nominal_z_m = nominal_z;
	hmd->log_level = U_LOGGING_INFO;

	// xrt_device methods.
	hmd->base.update_inputs = u_device_noop_update_inputs;
	hmd->base.get_tracked_pose = example_hmd_get_tracked_pose;
	hmd->base.get_view_poses = u_device_get_view_poses;
	hmd->base.get_visibility_mask = u_device_get_visibility_mask;
	hmd->base.destroy = example_hmd_destroy;
	hmd->base.name = XRT_DEVICE_GENERIC_HMD;
	hmd->base.device_type = XRT_DEVICE_TYPE_HMD;

	// Pose is delegated to the qwerty HMD (via pose_source), which already
	// includes the standing height. Mark the tracking origin OTHER so the
	// builder does not add a redundant Y=1.6 offset.
	hmd->base.tracking_origin->type = XRT_TRACKING_TYPE_OTHER;

	// Static pose: centered, at nominal viewing distance.
	hmd->pose.orientation.w = 1.0f;
	hmd->pose.position.z = -nominal_z; // Negative Z = looking at the display.

	hmd->base.hmd->view_count = 2;

	snprintf(hmd->base.str, XRT_DEVICE_NAME_LEN, "Example 3D Display");
	snprintf(hmd->base.serial, XRT_DEVICE_NAME_LEN, "example_display_0");

	// Rendering modes: 2D (mono) + 3D (stereo, side-by-side 2x1 atlas).
	// Every DisplayXR app creates ONE worst-case-sized swapchain and renders
	// per-mode tiles into subrects — see docs/specs/runtime/multiview-tiling.md.
	// VENDOR TODO: add your real modes here (e.g. an N-view lenticular mode
	// with tile_columns * tile_rows == view_count). Keep 2D as mode 0.
	hmd->base.rendering_mode_count = 2;

	// Mode 0: 2D (mono, full-res, 1x1 tile). Untracked (mode_flags = 0).
	hmd->base.rendering_modes[0].mode_index = 0;
	snprintf(hmd->base.rendering_modes[0].mode_name, XRT_DEVICE_NAME_LEN, "2D");
	hmd->base.rendering_modes[0].view_count = 1;
	hmd->base.rendering_modes[0].view_scale_x = 1.0f;
	hmd->base.rendering_modes[0].view_scale_y = 1.0f;
	hmd->base.rendering_modes[0].hardware_display_3d = false;
	hmd->base.rendering_modes[0].tile_columns = 1;
	hmd->base.rendering_modes[0].tile_rows = 1;
	hmd->base.rendering_modes[0].mode_flags = 0;

	// Mode 1: 3D (two views, 2x1 side-by-side tiles). This is the atlas the
	// example weavers below consume. mode_flags stays 0 because this template
	// advertises NO eye tracking (get_display_info reports 0). If your device
	// consumes live eye tracking, set XRT_RENDERING_MODE_FLAG_HAS_TRACKING here
	// AND advertise a non-zero supported_eye_tracking_modes in the plug-in —
	// the runtime asserts the two are consistent (#441).
	hmd->base.rendering_modes[1].mode_index = 1;
	snprintf(hmd->base.rendering_modes[1].mode_name, XRT_DEVICE_NAME_LEN, "Example3D");
	hmd->base.rendering_modes[1].view_count = 2;
	hmd->base.rendering_modes[1].view_scale_x = 0.5f;
	hmd->base.rendering_modes[1].view_scale_y = 1.0f;
	hmd->base.rendering_modes[1].hardware_display_3d = true;
	hmd->base.rendering_modes[1].tile_columns = 2;
	hmd->base.rendering_modes[1].tile_rows = 1;
	hmd->base.rendering_modes[1].mode_flags = 0;

	hmd->base.hmd->active_rendering_mode_index = 1; // Default to 3D.

	// Head pose input.
	hmd->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;

	// Display geometry via the split-side-by-side helper (sets up screens,
	// per-view viewports, and a symmetric FOV from the geometry below).
	struct u_device_simple_info info;
	info.display.w_pixels = pixel_w;
	info.display.h_pixels = pixel_h;
	info.display.w_meters = display_w_m;
	info.display.h_meters = display_h_m;
	info.lens_horizontal_separation_meters = ipd_m;
	info.lens_vertical_position_meters = display_h_m / 2.0f;

	// Per-view eye-box offsets (display-local space).
	{
		const float half_ipd = ipd_m / 2.0f;
		hmd->base.hmd->view_eye_offsets[0] = (struct xrt_vec3){-half_ipd, 0.0f, nominal_z};
		hmd->base.hmd->view_eye_offsets[1] = (struct xrt_vec3){half_ipd, 0.0f, nominal_z};
		for (uint32_t v = 2; v < XRT_MAX_VIEWS; v++) {
			hmd->base.hmd->view_eye_offsets[v] = (struct xrt_vec3){0.0f, 0.0f, nominal_z};
		}
	}

	// FOV from display geometry + viewing distance.
	float half_fov_h = atanf((display_w_m / 2.0f) / nominal_z);
	info.fov[0] = half_fov_h * 2.0f;
	info.fov[1] = half_fov_h * 2.0f;

	if (!u_device_setup_split_side_by_side(&hmd->base, &info)) {
		U_LOG_E("example_device: failed to set up display device info");
		example_hmd_destroy(&hmd->base);
		return NULL;
	}

	// No distortion for a flat 3D panel.
	u_distortion_mesh_set_none(&hmd->base);

	// Debug variables (surfaced by the runtime's u_var debug UI).
	u_var_add_root(hmd, "Example 3D Display", true);
	u_var_add_pose(hmd, &hmd->pose, "pose");
	u_var_add_f32(hmd, &hmd->display_width_m, "display_width_m");
	u_var_add_f32(hmd, &hmd->display_height_m, "display_height_m");
	u_var_add_f32(hmd, &hmd->nominal_z_m, "nominal_z_m");
	u_var_add_log_level(hmd, &hmd->log_level, "log_level");

	U_LOG_W("Created Example 3D display: %dx%d px, %.3fx%.3f m, nominal Z=%.2f m", pixel_w, pixel_h,
	        display_w_m, display_h_m, nominal_z);

	return &hmd->base;
}
