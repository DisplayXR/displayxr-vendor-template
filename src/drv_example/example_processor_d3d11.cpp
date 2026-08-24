// Copyright 2026, The DisplayXR Project
// SPDX-License-Identifier: Apache-2.0
/*!
 * @file
 * @brief  Example D3D11 display processor — STUB weave (no vendor SDK).
 *
 * Implements the @ref xrt_display_processor_d3d11 vtable. The compositor renders
 * all views into a tiled atlas texture and calls process_atlas() each frame; the
 * display processor turns that atlas into the panel's final output. On a real 3D
 * display that final output is an interlaced/woven lenticular pattern computed
 * from the vendor's lens calibration + live eye positions.
 *
 * THIS TEMPLATE does the simplest thing that proves the pixel path end-to-end:
 * a naive per-column interlace of the two atlas tiles (even output columns from
 * the left view, odd columns from the right view), with a 2D passthrough when
 * the atlas has a single tile. It is NOT a correct autostereoscopic weave — it
 * has no lens model, no phase, no eye tracking.
 *
 *   // VENDOR TODO: replace process_atlas' shader with your real weaver.
 *
 * **If your display weaves in its own hardware** (an FPGA or ASIC on the scaler
 * board) you do not write a weaver at all — you hand the chip a PACKED frame and
 * it weaves during scanout. Set `DXR_EXAMPLE_WEAVE_MODE=hardware` to see that
 * path: process_atlas becomes a straight passthrough, because the atlas the
 * compositor hands you ALREADY IS the packed frame. Declare tile geometry that
 * matches your chip's expected pack (2x1 for side-by-side half, 1x2 for
 * top-and-bottom, an NxM grid for a quilt — see example_device.c) and the atlas
 * comes out panel-sized with each view in its half. Such a plug-in must also
 * declare its weave scope; see example_dp_d3d11_get_scanout_caps below.
 *
 * Of the ~19 D3D11 DP vtable slots, only process_atlas + destroy are mandatory
 * (the rest are optional and NULL-safe via the runtime's XRT_DP_HAS_SLOT gate).
 * This file leaves all the optional slots NULL except is_alpha_native.
 *
 * @ingroup drv_example
 */

#include "example_interface.h"

#include "xrt/xrt_display_processor_d3d11.h"

#include "util/u_logging.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <cstdlib>
#include <cstring>


// Fullscreen quad vertex shader (4 vertices, triangle strip via SV_VertexID).
static const char *k_vs_source = R"(
struct VS_OUTPUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};
VS_OUTPUT main(uint id : SV_VertexID) {
    VS_OUTPUT o;
    o.uv  = float2(id & 1, id >> 1);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
)";

// STUB weave pixel shader: per-column interlace of the two atlas tiles.
// VENDOR TODO: replace with your lens-calibrated interlacing shader.
static const char *k_ps_source = R"(
cbuffer TileParams : register(b0) {
    float tile_cols_inv; // 1 / tile_columns
    float tile_rows_inv; // 1 / tile_rows
    float tile_cols;     // tile_columns as float
    float tile_rows;     // tile_rows as float
};
Texture2D    atlas_tex : register(t0);
SamplerState samp      : register(s0);

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    // 2D / single-tile atlas: straight passthrough of tile 0.
    if (tile_cols * tile_rows <= 1.0) {
        return atlas_tex.Sample(samp, float2(uv.x * tile_cols_inv, uv.y * tile_rows_inv));
    }
    // 3D stub: even output columns show the left view (tile 0), odd columns the
    // right view (tile 1). A real weaver replaces this parity test with its
    // lenticular sub-pixel mapping + eye-tracked phase.
    uint  screen_col = (uint)pos.x;
    float eye        = (screen_col & 1u) ? 1.0 : 0.0; // 0 = left tile, 1 = right tile.
    float2 src = float2((uv.x + eye) * tile_cols_inv, uv.y * tile_rows_inv);
    return atlas_tex.Sample(samp, src);
}
)";


// HARDWARE-WEAVE pixel shader (DXR_EXAMPLE_WEAVE_MODE=hardware): pass the atlas
// through unchanged.
//
// This looks too simple to be right, and it is the whole point. The compositor
// already laid the views out as a tile_columns x tile_rows grid at panel size —
// which, when your declared tile geometry matches what your chip expects, IS
// the packed frame (2x1 @ 0.5,1.0 => left view in the left half, right view in
// the right half, at exactly the target's dimensions). There is nothing to
// rearrange. A real hardware-weaving DP does the same blit plus whatever
// signalling its chip needs: a watermark row stamped here, or a sideband
// command sent from request_display_mode().
static const char *k_ps_hw_pack_source = R"(
Texture2D    atlas_tex : register(t0);
SamplerState samp      : register(s0);

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return atlas_tex.Sample(samp, uv);
}
)";


struct tile_params_cb
{
	float tile_cols_inv;
	float tile_rows_inv;
	float tile_cols;
	float tile_rows;
};

/*!
 * Implementation struct. The vtable @ref base MUST be the first member so a
 * `struct xrt_display_processor_d3d11 *` and this pointer are interchangeable.
 */
struct example_dp_d3d11
{
	struct xrt_display_processor_d3d11 base;
	ID3D11VertexShader *vs;
	ID3D11PixelShader *ps;
	ID3D11SamplerState *sampler;
	ID3D11Buffer *tile_cb;
	//! True when built in DXR_EXAMPLE_WEAVE_MODE=hardware. Logging only —
	//! the mode is baked into which pixel shader `ps` holds.
	bool hardware_weave;
};

/*!
 * `DXR_EXAMPLE_WEAVE_MODE=hardware` selects the packed-frame passthrough for a
 * display whose FPGA/ASIC does the weave. Anything else (including unset) keeps
 * the GPU stub weaver.
 */
static bool
example_hardware_weave_from_env(void)
{
	const char *v = getenv("DXR_EXAMPLE_WEAVE_MODE");
	return v != nullptr && strcmp(v, "hardware") == 0;
}

static inline struct example_dp_d3d11 *
example_dp(struct xrt_display_processor_d3d11 *xdp)
{
	return reinterpret_cast<struct example_dp_d3d11 *>(xdp);
}


/*
 *
 * Vtable methods.
 *
 */

static void
example_dp_d3d11_process_atlas(struct xrt_display_processor_d3d11 *xdp,
                               void *d3d11_context,
                               void *atlas_srv,
                               uint32_t view_width,
                               uint32_t view_height,
                               uint32_t tile_columns,
                               uint32_t tile_rows,
                               uint32_t format,
                               uint32_t target_width,
                               uint32_t target_height,
                               int32_t canvas_offset_x,
                               int32_t canvas_offset_y,
                               uint32_t canvas_width,
                               uint32_t canvas_height)
{
	(void)view_width;
	(void)view_height;
	(void)format;
	// VENDOR TODO: a real weaver uses the canvas rect to keep the interlace
	// phase locked when the app draws to a sub-region of the panel. The stub
	// ignores it and fills the whole render target.
	(void)canvas_offset_x;
	(void)canvas_offset_y;
	(void)canvas_width;
	(void)canvas_height;

	struct example_dp_d3d11 *dp = example_dp(xdp);
	ID3D11DeviceContext *ctx = static_cast<ID3D11DeviceContext *>(d3d11_context);
	ID3D11ShaderResourceView *srv = static_cast<ID3D11ShaderResourceView *>(atlas_srv);
	if (ctx == nullptr || srv == nullptr) {
		return;
	}

	// The output render target is already bound by the compositor
	// (OMSetRenderTargets); we only set the viewport + draw a fullscreen quad.
	tile_params_cb cb = {};
	cb.tile_cols_inv = (tile_columns > 0) ? (1.0f / (float)tile_columns) : 1.0f;
	cb.tile_rows_inv = (tile_rows > 0) ? (1.0f / (float)tile_rows) : 1.0f;
	cb.tile_cols = (float)tile_columns;
	cb.tile_rows = (float)tile_rows;
	ctx->UpdateSubresource(dp->tile_cb, 0, nullptr, &cb, 0, 0);

	D3D11_VIEWPORT vp = {};
	vp.Width = (float)target_width;
	vp.Height = (float)target_height;
	vp.MaxDepth = 1.0f;
	ctx->RSSetViewports(1, &vp);

	ctx->VSSetShader(dp->vs, nullptr, 0);
	ctx->PSSetShader(dp->ps, nullptr, 0);
	ctx->PSSetSamplers(0, 1, &dp->sampler);
	ctx->PSSetShaderResources(0, 1, &srv);
	ctx->PSSetConstantBuffers(0, 1, &dp->tile_cb);

	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	ctx->IASetInputLayout(nullptr);
	ctx->Draw(4, 0);

	// Unbind the atlas SRV to avoid D3D11 read/write hazard warnings.
	ID3D11ShaderResourceView *null_srv = nullptr;
	ctx->PSSetShaderResources(0, 1, &null_srv);
}

#ifdef XRT_DP_D3D11_HAS_SCANOUT_CAPS
/*!
 * Declare how much of the panel this DP's output transform covers.
 *
 * A GPU weaver produces final pixels for the canvas it was handed, so it is
 * `CANVAS` and could equally leave this slot NULL — that is what the runtime
 * assumes for any plug-in that doesn't implement it. A plug-in for a display
 * that weaves in hardware MUST answer honestly, because the two hardware shapes
 * have completely different windowing consequences:
 *
 *  - `REGION` — your chip accepts a "weave only this rect, pass the rest
 *    through" descriptor. Windowed apps work; implement the zone slots
 *    (get_local_zone_caps / publish_local_zone_mask) to push the rect, and
 *    snap_window_rect to keep placement on your lens phase.
 *  - `SCANOUT` — your chip transforms the entire incoming frame. Only a
 *    panel-scoped (fullscreen) presentation can be correct, and saying so is
 *    what makes the runtime log a usable diagnostic instead of shipping a frame
 *    your chip will shred.
 *
 * `DXR_EXAMPLE_WEAVE_SCOPE=canvas|region|scanout` drives it here so the routing
 * can be exercised without hardware. A real plug-in returns a constant.
 *
 * The #ifdef keeps this template buildable against a runtime pin older than the
 * slot; the macro is defined by xrt_display_processor_d3d11.h once the pin
 * carries it.
 */
static bool
example_dp_d3d11_get_scanout_caps(struct xrt_display_processor_d3d11 *xdp, struct xrt_dp_scanout_caps *out_caps)
{
	(void)xdp;
	// Honour the caller's struct_size — never write past what the runtime
	// allocated (ADR-020).
	if (out_caps == nullptr || out_caps->struct_size < XRT_DP_SCANOUT_CAPS_SIZE_V1) {
		return false;
	}

	const char *v = getenv("DXR_EXAMPLE_WEAVE_SCOPE");
	enum xrt_dp_weave_scope scope = XRT_DP_WEAVE_SCOPE_CANVAS;
	if (v != nullptr && strcmp(v, "region") == 0) {
		scope = XRT_DP_WEAVE_SCOPE_REGION;
	} else if (v != nullptr && strcmp(v, "scanout") == 0) {
		scope = XRT_DP_WEAVE_SCOPE_SCANOUT;
	}

	out_caps->weave_scope = static_cast<uint32_t>(scope);
	for (size_t i = 0; i < sizeof(out_caps->reserved) / sizeof(out_caps->reserved[0]); i++) {
		out_caps->reserved[i] = 0; // reserved words MUST be zeroed.
	}
	return true;
}
#endif // XRT_DP_D3D11_HAS_SCANOUT_CAPS

/*!
 * This DP samples the atlas and writes its alpha straight through, so it is
 * alpha-native (no post-weave alpha reconstruction needed). Optional slot.
 */
static bool
example_dp_d3d11_is_alpha_native(struct xrt_display_processor_d3d11 *xdp)
{
	(void)xdp;
	return true;
}

static void
example_dp_d3d11_destroy(struct xrt_display_processor_d3d11 *xdp)
{
	struct example_dp_d3d11 *dp = example_dp(xdp);
	if (dp->vs) {
		dp->vs->Release();
	}
	if (dp->ps) {
		dp->ps->Release();
	}
	if (dp->sampler) {
		dp->sampler->Release();
	}
	if (dp->tile_cb) {
		dp->tile_cb->Release();
	}
	free(dp);
}


/*
 *
 * Helper.
 *
 */

static HRESULT
compile_shader(const char *src, const char *entry, const char *target, ID3DBlob **out_blob)
{
	ID3DBlob *err = nullptr;
	HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, target, 0, 0, out_blob, &err);
	if (FAILED(hr) && err != nullptr) {
		U_LOG_E("example_dp D3D11: shader compile error: %s", static_cast<const char *>(err->GetBufferPointer()));
		err->Release();
	}
	return hr;
}


/*
 *
 * Factory — matches xrt_dp_factory_d3d11_fn_t.
 *
 */

extern "C" xrt_result_t
example_dp_factory_d3d11(void *d3d11_device,
                         void *d3d11_context,
                         void *window_handle,
                         struct xrt_display_processor_d3d11 **out_xdp)
{
	(void)d3d11_context;
	(void)window_handle;

	if (out_xdp == nullptr) {
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}
	ID3D11Device *device = static_cast<ID3D11Device *>(d3d11_device);
	if (device == nullptr) {
		U_LOG_E("example_dp D3D11: a D3D11 device is required");
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	struct example_dp_d3d11 *dp = static_cast<struct example_dp_d3d11 *>(calloc(1, sizeof(*dp)));
	if (dp == nullptr) {
		return XRT_ERROR_ALLOCATION;
	}

	// ADR-020 rule 1: advertise the FULL vtable size so the runtime knows which
	// appended slots this build carries. calloc zeroed reserved_0 + every
	// optional slot we don't set (they read back as absent/NULL).
	dp->base.struct_size = static_cast<uint32_t>(sizeof(struct xrt_display_processor_d3d11));
	dp->base.process_atlas = example_dp_d3d11_process_atlas; // mandatory
	dp->base.destroy = example_dp_d3d11_destroy;             // mandatory
	dp->base.is_alpha_native = example_dp_d3d11_is_alpha_native;
#ifdef XRT_DP_D3D11_HAS_SCANOUT_CAPS
	dp->base.get_scanout_caps = example_dp_d3d11_get_scanout_caps;
#endif
	// All other slots (get_predicted_eye_positions, request_display_mode,
	// zone publish, set_transparent_background, snap_window_rect, ...) stay
	// NULL — they are optional. VENDOR TODO: implement the ones your product
	// needs (e.g. get_predicted_eye_positions for MANAGED eye tracking,
	// set_transparent_background for see-through, request_display_mode for a
	// hardware 2D/3D toggle).

	dp->hardware_weave = example_hardware_weave_from_env();
	U_LOG_I("example_dp D3D11: %s", dp->hardware_weave
	                                    ? "hardware-weave mode — emitting a packed frame, the panel weaves it"
	                                    : "GPU stub weave (set DXR_EXAMPLE_WEAVE_MODE=hardware for the other shape)");

	ID3DBlob *blob = nullptr;
	if (FAILED(compile_shader(k_vs_source, "main", "vs_5_0", &blob))) {
		free(dp);
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}
	HRESULT hr = device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &dp->vs);
	blob->Release();
	if (FAILED(hr)) {
		free(dp);
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	if (FAILED(compile_shader(dp->hardware_weave ? k_ps_hw_pack_source : k_ps_source, "main", "ps_5_0",
	                          &blob))) {
		example_dp_d3d11_destroy(&dp->base);
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}
	hr = device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &dp->ps);
	blob->Release();
	if (FAILED(hr)) {
		example_dp_d3d11_destroy(&dp->base);
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	D3D11_SAMPLER_DESC sd = {};
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(device->CreateSamplerState(&sd, &dp->sampler))) {
		example_dp_d3d11_destroy(&dp->base);
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = sizeof(tile_params_cb);
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	if (FAILED(device->CreateBuffer(&bd, nullptr, &dp->tile_cb))) {
		example_dp_d3d11_destroy(&dp->base);
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	U_LOG_W("example_dp D3D11: created STUB interlace weaver — VENDOR TODO: replace process_atlas");
	*out_xdp = &dp->base;
	return XRT_SUCCESS;
}
