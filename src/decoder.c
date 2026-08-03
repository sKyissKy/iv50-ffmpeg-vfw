/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "iv50_vfw.h"

#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct Iv50Decoder {
    AVCodecContext *codec_context;
    AVFrame *frame;
    AVPacket *packet;
    struct SwsContext *sws_context;
    BITMAPINFOHEADER output_format;
    BOOL begun;
};

static void iv50_decoder_reset(Iv50Decoder *decoder)
{
    if (decoder == NULL) {
        return;
    }

    sws_freeContext(decoder->sws_context);
    decoder->sws_context = NULL;
    av_packet_free(&decoder->packet);
    av_frame_free(&decoder->frame);
    avcodec_free_context(&decoder->codec_context);
    ZeroMemory(&decoder->output_format, sizeof(decoder->output_format));
    decoder->begun = FALSE;
}

Iv50Decoder *iv50_decoder_create(void)
{
    return (Iv50Decoder *)calloc(1, sizeof(Iv50Decoder));
}

void iv50_decoder_destroy(Iv50Decoder *decoder)
{
    if (decoder == NULL) {
        return;
    }
    iv50_decoder_reset(decoder);
    free(decoder);
}

int iv50_decoder_begin(
    Iv50Decoder *decoder,
    const BITMAPINFOHEADER *input,
    const BITMAPINFOHEADER *output)
{
    const AVCodec *codec;
    size_t extra_size;
    const uint8_t *extra_source;
    int result;

    if (decoder == NULL ||
        !iv50_is_input_format_supported(input) ||
        !iv50_is_output_format_supported(input, output)) {
        return ICERR_BADFORMAT;
    }

    iv50_decoder_reset(decoder);

    codec = avcodec_find_decoder(AV_CODEC_ID_INDEO5);
    if (codec == NULL) {
        return ICERR_UNSUPPORTED;
    }

    decoder->codec_context = avcodec_alloc_context3(codec);
    decoder->frame = av_frame_alloc();
    decoder->packet = av_packet_alloc();
    if (decoder->codec_context == NULL ||
        decoder->frame == NULL ||
        decoder->packet == NULL) {
        iv50_decoder_reset(decoder);
        return ICERR_MEMORY;
    }

    decoder->codec_context->codec_tag = IV50_FOURCC;
    decoder->codec_context->width = input->biWidth;
    decoder->codec_context->height = input->biHeight < 0
        ? -input->biHeight
        : input->biHeight;

    extra_size = input->biSize - sizeof(BITMAPINFOHEADER);
    if (extra_size != 0) {
        decoder->codec_context->extradata = (uint8_t *)av_mallocz(
            extra_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (decoder->codec_context->extradata == NULL) {
            iv50_decoder_reset(decoder);
            return ICERR_MEMORY;
        }
        extra_source = (const uint8_t *)input + sizeof(BITMAPINFOHEADER);
        memcpy(decoder->codec_context->extradata, extra_source, extra_size);
        decoder->codec_context->extradata_size = (int)extra_size;
    }

    result = avcodec_open2(decoder->codec_context, codec, NULL);
    if (result < 0) {
        iv50_decoder_reset(decoder);
        return ICERR_BADFORMAT;
    }

    decoder->output_format = *output;
    decoder->begun = TRUE;
    return ICERR_OK;
}

static int iv50_convert_frame(
    Iv50Decoder *decoder,
    const AVFrame *frame,
    uint8_t *output)
{
    enum AVPixelFormat destination_format;
    uint8_t *destination_data[4] = { 0 };
    int destination_stride[4] = { 0 };
    DWORD stride;
    DWORD image_size;
    LONG height;
    int converted_rows;

    if (decoder == NULL || frame == NULL || output == NULL) {
        return ICERR_BADPARAM;
    }
    if (frame->width != decoder->output_format.biWidth ||
        frame->height != (decoder->output_format.biHeight < 0
            ? -decoder->output_format.biHeight
            : decoder->output_format.biHeight)) {
        return ICERR_BADFORMAT;
    }
    if (!iv50_calculate_image_size(
            decoder->output_format.biWidth,
            decoder->output_format.biHeight,
            decoder->output_format.biBitCount,
            &stride,
            &image_size)) {
        return ICERR_BADFORMAT;
    }

    destination_format = decoder->output_format.biBitCount == 24
        ? AV_PIX_FMT_BGR24
        : AV_PIX_FMT_BGRA;
    decoder->sws_context = sws_getCachedContext(
        decoder->sws_context,
        frame->width,
        frame->height,
        (enum AVPixelFormat)frame->format,
        frame->width,
        frame->height,
        destination_format,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL);
    if (decoder->sws_context == NULL) {
        return ICERR_MEMORY;
    }

    memset(output, 0, image_size);
    height = decoder->output_format.biHeight < 0
        ? -decoder->output_format.biHeight
        : decoder->output_format.biHeight;
    if (decoder->output_format.biHeight > 0) {
        destination_data[0] = output + ((size_t)stride * (height - 1));
        destination_stride[0] = -(int)stride;
    } else {
        destination_data[0] = output;
        destination_stride[0] = (int)stride;
    }

    converted_rows = sws_scale(
        decoder->sws_context,
        (const uint8_t *const *)frame->data,
        frame->linesize,
        0,
        frame->height,
        destination_data,
        destination_stride);
    return converted_rows == frame->height ? ICERR_OK : ICERR_ERROR;
}

int iv50_decoder_decode(
    Iv50Decoder *decoder,
    const ICDECOMPRESS *request)
{
    DWORD input_size;
    DWORD output_stride;
    DWORD output_size;
    BOOL suppress_output;
    int result;

    if (decoder == NULL || !decoder->begun || request == NULL ||
        request->lpbiInput == NULL || request->lpInput == NULL ||
        request->lpbiOutput == NULL) {
        return ICERR_BADPARAM;
    }
    if (!iv50_is_input_format_supported(request->lpbiInput) ||
        !iv50_is_output_format_supported(
            request->lpbiInput,
            request->lpbiOutput)) {
        return ICERR_BADFORMAT;
    }
    if (request->lpbiOutput->biWidth != decoder->output_format.biWidth ||
        request->lpbiOutput->biHeight != decoder->output_format.biHeight ||
        request->lpbiOutput->biBitCount != decoder->output_format.biBitCount ||
        request->lpbiOutput->biCompression != decoder->output_format.biCompression) {
        return ICERR_BADFORMAT;
    }
    if (!iv50_calculate_image_size(
            request->lpbiOutput->biWidth,
            request->lpbiOutput->biHeight,
            request->lpbiOutput->biBitCount,
            &output_stride,
            &output_size) ||
        request->lpbiOutput->biSizeImage < output_size) {
        return ICERR_BADPARAM;
    }

    suppress_output = (request->dwFlags &
        (ICDECOMPRESS_HURRYUP | ICDECOMPRESS_UPDATE | ICDECOMPRESS_PREROLL)) != 0;
    if (request->lpOutput == NULL && !suppress_output) {
        return ICERR_BADPARAM;
    }

    input_size = request->lpbiInput->biSizeImage;
    if (input_size == 0 || input_size > IV50_MAX_COMPRESSED_FRAME ||
        input_size > INT_MAX) {
        return ICERR_BADPARAM;
    }

    av_packet_unref(decoder->packet);
    result = av_new_packet(decoder->packet, (int)input_size);
    if (result < 0) {
        return ICERR_MEMORY;
    }
    memcpy(decoder->packet->data, request->lpInput, input_size);
    if ((request->dwFlags & ICDECOMPRESS_NOTKEYFRAME) == 0) {
        decoder->packet->flags |= AV_PKT_FLAG_KEY;
    }

    result = avcodec_send_packet(decoder->codec_context, decoder->packet);
    if (result < 0) {
        return ICERR_BADFORMAT;
    }

    av_frame_unref(decoder->frame);
    result = avcodec_receive_frame(decoder->codec_context, decoder->frame);
    if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
        return ICERR_ERROR;
    }
    if (result < 0) {
        return ICERR_BADFORMAT;
    }

    if (suppress_output || request->lpOutput == NULL) {
        return ICERR_OK;
    }

    return iv50_convert_frame(
        decoder,
        decoder->frame,
        (uint8_t *)request->lpOutput);
}

void iv50_decoder_end(Iv50Decoder *decoder)
{
    iv50_decoder_reset(decoder);
}
