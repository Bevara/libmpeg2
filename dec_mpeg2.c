/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / MPEG-1/2 video decoder filter
 *  based on libmpeg2 (http://libmpeg2.sourceforge.net/)
 *
 */

#include <gpac/filters.h>
#include <string.h>
#include <stdio.h>

#include <mpeg2.h>

typedef struct
{
	GF_FilterPid *ipid, *opid;

	Bool is_playing;
	u32 codec_id;

	u32 width, height, chroma_width, chroma_height;
	u32 frame_period;
	u64 next_cts;
	Bool seq_ready;
} GF_Mpeg2DecCtx;

static GF_Err mpeg2vdec_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	const GF_PropertyValue *prop;
	GF_Mpeg2DecCtx *ctx = (GF_Mpeg2DecCtx *)gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	prop = gf_filter_pid_get_property(pid, GF_PROP_PID_CODECID);
	if (!prop)
		return GF_NOT_SUPPORTED;
	ctx->ipid = pid;

	if (!ctx->opid)
	{
		ctx->opid = gf_filter_pid_new(filter);
	}

	gf_filter_pid_copy_properties(ctx->opid, ctx->ipid);
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_YUV));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_TIMESCALE, &PROP_UINT(27000000));

	return GF_OK;
}

static void mpeg2vdec_send_frame(GF_Mpeg2DecCtx *ctx, const mpeg2_fbuf_t *fbuf)
{
	GF_FilterPacket *dst_pck;
	u8 *output;
	u32 y_size = ctx->width * ctx->height;
	u32 c_size = ctx->chroma_width * ctx->chroma_height;
	u32 out_size = y_size + 2 * c_size;

	dst_pck = gf_filter_pck_new_alloc(ctx->opid, out_size, &output);
	if (!dst_pck) return;

	memcpy(output, fbuf->buf[0], y_size);
	memcpy(output + y_size, fbuf->buf[1], c_size);
	memcpy(output + y_size + c_size, fbuf->buf[2], c_size);

	gf_filter_pck_set_cts(dst_pck, ctx->next_cts);
	gf_filter_pck_set_duration(dst_pck, ctx->frame_period ? ctx->frame_period : 450450);
	gf_filter_pck_set_sap(dst_pck, GF_FILTER_SAP_1);
	gf_filter_pck_send(dst_pck);

	ctx->next_cts += ctx->frame_period ? ctx->frame_period : 450450;
}

static GF_Err mpeg2vdec_process(GF_Filter *filter)
{
	GF_FilterPacket *pck;
	u8 *data;
	u32 size;
	mpeg2dec_t *decoder;
	const mpeg2_info_t *info;
	mpeg2_state_t state;
	GF_Mpeg2DecCtx *ctx = (GF_Mpeg2DecCtx *)gf_filter_get_udta(filter);

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);


	if (!data)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_IO_ERR;
	}

	decoder = mpeg2_init();
	if (!decoder)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OUT_OF_MEM;
	}
	info = mpeg2_info(decoder);

	mpeg2_buffer(decoder, data, data + size);

	ctx->next_cts = 0;
	ctx->seq_ready = GF_FALSE;

	while (1)
	{
		state = mpeg2_parse(decoder);
		if (state == STATE_BUFFER)
			break;

		switch (state)
		{
		case STATE_SEQUENCE:
			ctx->width = info->sequence->width;
			ctx->height = info->sequence->height;
			ctx->chroma_width = info->sequence->chroma_width;
			ctx->chroma_height = info->sequence->chroma_height;
			ctx->frame_period = info->sequence->frame_period;

			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_WIDTH, &PROP_UINT(ctx->width));
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_HEIGHT, &PROP_UINT(ctx->height));
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STRIDE, &PROP_UINT(ctx->width));
			ctx->seq_ready = GF_TRUE;
			break;
		case STATE_SLICE:
		case STATE_END:
		case STATE_INVALID_END:
			if (ctx->seq_ready && info->display_fbuf)
			{
				mpeg2vdec_send_frame(ctx, info->display_fbuf);
			}
			break;
		default:
			break;
		}
	}

	mpeg2_close(decoder);
	gf_filter_pid_drop_packet(ctx->ipid);


	if (!ctx->seq_ready)
		return GF_NON_COMPLIANT_BITSTREAM;

	gf_filter_pid_set_eos(ctx->opid);
	return GF_EOS;
}

static const GF_FilterCapability Mpeg2DecCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_CODECID, GF_4CC('M', '2', 'V', ' ')),
		CAP_BOOL(GF_CAPS_INPUT_EXCLUDED, GF_PROP_PID_UNFRAMED, GF_TRUE),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

GF_FilterRegister Mpeg2DecoderRegister = {
	.name = "mpeg2vdec",
	GF_FS_SET_DESCRIPTION("MPEG-1/2 video decoder")
		GF_FS_SET_HELP("This filter decodes raw MPEG-1/2 video elementary streams using libmpeg2.")
			.private_size = sizeof(GF_Mpeg2DecCtx),
	SETCAPS(Mpeg2DecCaps),
	.configure_pid = mpeg2vdec_configure_pid,
	.process = mpeg2vdec_process,
};

const GF_FilterRegister * EMSCRIPTEN_KEEPALIVE dynCall_mpeg2vdec_register(GF_FilterSession *session)
{
	return &Mpeg2DecoderRegister;
}

#include "filter_register.h"
__attribute__((constructor))
void register_mpeg2vdec(void) {
    gf_filter_auto_register("mpeg2vdec", dynCall_mpeg2vdec_register);
}
