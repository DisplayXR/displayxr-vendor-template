// Copyright 2026, The DisplayXR Project
// SPDX-License-Identifier: Apache-2.0
/*!
 * @file
 * @brief  Example Vulkan display processor — STUB weave (no vendor SDK).
 *
 * Implements the @ref xrt_display_processor_vk vtable variant. On Vulkan the
 * generic @ref xrt_display_processor IS the Vulkan interface: process_atlas
 * records commands into the compositor's VkCommandBuffer. The `_vk` variant
 * embeds that base at offset 0 and appends a few optional slots; a plug-in opts
 * in by allocating the variant and setting `base.struct_size` to
 * `sizeof(struct xrt_display_processor_vk)`.
 *
 * THIS TEMPLATE does a passthrough: it blits the first atlas tile (view 0) to
 * the whole target with a linear filter — enough to prove the Vulkan pixel path
 * without any shaders/pipelines. It is NOT a weave: no interlacing, no lens
 * model, no eye tracking, and it only shows one eye.
 *
 *   // VENDOR TODO: replace process_atlas with a render pass + interlacing
 *   //              pipeline (see the runtime's sim_display_processor.c for the
 *   //              full descriptor-set / pipeline shape).
 *
 * The struct_size gate makes this ABI-correct: a newer runtime treats slots
 * past our struct_size as absent; an older runtime ignores slots it doesn't
 * know. Only base.process_atlas + base.destroy are mandatory.
 *
 * The Vulkan bundle handed to the factory (`void *vk_bundle`) is the runtime's
 * `struct vk_bundle` — a dispatch table of function pointers (vk->vkCmdBlitImage
 * etc.) plus vk->device. Including "vk/vk_helpers.h" (an aux header the runtime
 * exposes to plug-ins) is exactly how the in-tree sim_display VK DP consumes it.
 *
 * @ingroup drv_example
 */

#include "example_interface.h"

#include "xrt/xrt_display_processor_vk.h"

#include "vk/vk_helpers.h"
#include "util/u_logging.h"

#include <stdlib.h>
#include <string.h>


/*!
 * Implementation struct. The @ref base variant vtable MUST be first (offset 0),
 * and its embedded `base.base` must in turn be at offset 0, so a
 * `struct xrt_display_processor *` handed to process_atlas casts straight back
 * to this pointer.
 */
struct example_dp_vk
{
	struct xrt_display_processor_vk base; //!< variant vtable (embeds the generic base at offset 0)
	struct vk_bundle *vk;                 //!< runtime's Vulkan dispatch bundle
};

static inline struct example_dp_vk *
example_dp_vk(struct xrt_display_processor *xdp)
{
	// xdp points at &dp->base.base (the generic base), which is at offset 0 of
	// the variant, which is at offset 0 of example_dp_vk.
	return (struct example_dp_vk *)xdp;
}


/*
 *
 * Small image-barrier helper (kept simple/robust over optimal).
 *
 */

static void
image_barrier(struct vk_bundle *vk,
              VkCommandBuffer cmd,
              VkImage image,
              VkImageLayout old_layout,
              VkImageLayout new_layout)
{
	VkImageMemoryBarrier b = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	    .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
	    .dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
	    .oldLayout = old_layout,
	    .newLayout = new_layout,
	    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .image = image,
	    .subresourceRange =
	        {
	            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	            .baseMipLevel = 0,
	            .levelCount = 1,
	            .baseArrayLayer = 0,
	            .layerCount = 1,
	        },
	};
	// ALL_COMMANDS is heavy-handed but correct — the vendor tightens these once
	// the real weave pipeline defines its actual stage dependencies.
	vk->vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
	                         NULL, 0, NULL, 1, &b);
}


/*
 *
 * Vtable methods.
 *
 */

static void
example_dp_vk_process_atlas(struct xrt_display_processor *xdp,
                            VkCommandBuffer cmd_buffer,
                            VkImage_XDP atlas_image,
                            VkImageView atlas_view,
                            uint32_t view_width,
                            uint32_t view_height,
                            uint32_t tile_columns,
                            uint32_t tile_rows,
                            VkFormat_XDP view_format,
                            VkFramebuffer target_fb,
                            VkImage_XDP target_image,
                            uint32_t target_width,
                            uint32_t target_height,
                            VkFormat_XDP target_format,
                            int32_t canvas_offset_x,
                            int32_t canvas_offset_y,
                            uint32_t canvas_width,
                            uint32_t canvas_height)
{
	struct example_dp_vk *dp = example_dp_vk(xdp);
	struct vk_bundle *vk = dp->vk;

	// The stub blits raw images, so it uses atlas_image/target_image directly
	// and needs neither the sampled view nor the framebuffer a render-pass DP
	// would use. A real weaver samples atlas_view in a fragment shader and
	// renders into target_fb.
	(void)atlas_view;
	(void)view_format;
	(void)target_fb;
	(void)target_format;
	(void)tile_columns;
	(void)tile_rows;
	// VENDOR TODO: keep the interlace phase locked to the canvas sub-rect.
	(void)canvas_offset_x;
	(void)canvas_offset_y;
	(void)canvas_width;
	(void)canvas_height;

	// On 64-bit these forward-declared handle types alias the real Vulkan
	// handles; the cast is a no-op reinterpret.
	VkImage atlas = (VkImage)atlas_image;
	VkImage target = (VkImage)target_image;
	if (vk == NULL || cmd_buffer == VK_NULL_HANDLE || atlas == VK_NULL_HANDLE || target == VK_NULL_HANDLE) {
		return;
	}

	// Source region = view 0 (top-left tile). 2D atlas ⟹ the whole image;
	// 3D 2x1 atlas ⟹ the left eye. This shows ONE view — it is a passthrough,
	// not a stereo weave.
	int32_t src_w = (int32_t)view_width;
	int32_t src_h = (int32_t)view_height;

	// Layouts assumed by convention (verify against your compositor build):
	//   atlas  arrives in SHADER_READ_ONLY_OPTIMAL (sampled input);
	//   target contents are irrelevant (we overwrite all of it) so UNDEFINED
	//   is a safe old layout, and the compositor presents it afterward, so we
	//   leave it in PRESENT_SRC_KHR.
	// VENDOR TODO: confirm/adjust these to match your present path.
	image_barrier(vk, cmd_buffer, atlas, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	image_barrier(vk, cmd_buffer, target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkImageBlit region = {
	    .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
	    .srcOffsets = {{0, 0, 0}, {src_w, src_h, 1}},
	    .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
	    .dstOffsets = {{0, 0, 0}, {(int32_t)target_width, (int32_t)target_height, 1}},
	};
	vk->vkCmdBlitImage(cmd_buffer, atlas, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
	                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);

	// Restore layouts for the compositor.
	image_barrier(vk, cmd_buffer, atlas, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	image_barrier(vk, cmd_buffer, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

static bool
example_dp_vk_is_alpha_native(struct xrt_display_processor *xdp)
{
	(void)xdp;
	// The blit copies atlas alpha straight to the target — alpha-native.
	return true;
}

static void
example_dp_vk_destroy(struct xrt_display_processor *xdp)
{
	struct example_dp_vk *dp = example_dp_vk(xdp);
	// The stub owns no Vulkan objects (no pipelines/render pass/descriptors).
	// VENDOR TODO: destroy your pipeline/render-pass/sampler/descriptor-pool here.
	free(dp);
}


/*
 *
 * Factory — matches xrt_dp_factory_vk_fn_t.
 *
 */

xrt_result_t
example_dp_factory_vk(void *vk_bundle,
                      void *vk_cmd_pool,
                      void *window_handle,
                      int32_t target_format,
                      struct xrt_display_processor **out_xdp)
{
	(void)vk_cmd_pool;
	(void)window_handle;
	(void)target_format; // the stub blits, so it needs no format-specific pipeline

	if (out_xdp == NULL) {
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}
	struct vk_bundle *vk = (struct vk_bundle *)vk_bundle;
	if (vk == NULL) {
		U_LOG_E("example_dp VK: a vk_bundle is required");
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	struct example_dp_vk *dp = calloc(1, sizeof(*dp));
	if (dp == NULL) {
		return XRT_ERROR_ALLOCATION;
	}
	dp->vk = vk;

	// ADR-020: advertise the VARIANT size (sizeof xrt_display_processor_vk) in
	// the EMBEDDED base's struct_size header. That's what tells the runtime to
	// treat this as the `_vk` variant and gates the appended optional slots.
	// calloc zeroed reserved_0 + every slot we leave NULL.
	dp->base.base.struct_size = (uint32_t)sizeof(struct xrt_display_processor_vk);
	dp->base.base.process_atlas = example_dp_vk_process_atlas; // mandatory
	dp->base.base.destroy = example_dp_vk_destroy;             // mandatory
	dp->base.base.is_alpha_native = example_dp_vk_is_alpha_native;
	// Optional base slots (get_predicted_eye_positions, get_render_pass,
	// request_display_mode, on_pause/on_resume, zone publish, ...) and the
	// appended variant slots (set_transparent_background, notify_target_recreated,
	// set_shared_texture_present) stay NULL — all NULL-safe via XRT_DP_HAS_SLOT.
	// VENDOR TODO: implement the ones your product needs.

	U_LOG_W("example_dp VK: created STUB passthrough blit — VENDOR TODO: replace process_atlas");
	*out_xdp = &dp->base.base;
	return XRT_SUCCESS;
}
