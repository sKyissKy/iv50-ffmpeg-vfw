/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef IV50_VFW_H
#define IV50_VFW_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vfw.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IV50_FOURCC mmioFOURCC('I', 'V', '5', '0')
#define IV50_VCM_HANDLER mmioFOURCC('i', 'v', '5', '0')
#define IV50_MAX_DIMENSION 32768
#define IV50_MAX_EXTRADATA (1024U * 1024U)
#define IV50_MAX_COMPRESSED_FRAME (256U * 1024U * 1024U)

typedef struct Iv50Decoder Iv50Decoder;

typedef struct Iv50VfwInstance {
    Iv50Decoder *decoder;
    BITMAPINFOHEADER input_format;
    BITMAPINFOHEADER output_format;
    ICDRAWBEGIN draw_begin;
    uint8_t *draw_buffer;
    DWORD draw_buffer_size;
    BOOL begun;
    BOOL draw_begun;
} Iv50VfwInstance;

BOOL iv50_is_input_format_supported(const BITMAPINFOHEADER *input);
BOOL iv50_is_output_format_supported(
    const BITMAPINFOHEADER *input,
    const BITMAPINFOHEADER *output);
BOOL iv50_calculate_image_size(
    LONG width,
    LONG height,
    WORD bit_count,
    DWORD *stride,
    DWORD *image_size);
LRESULT iv50_get_output_format(
    const BITMAPINFOHEADER *input,
    BITMAPINFOHEADER *output);

Iv50Decoder *iv50_decoder_create(void);
void iv50_decoder_destroy(Iv50Decoder *decoder);
int iv50_decoder_begin(
    Iv50Decoder *decoder,
    const BITMAPINFOHEADER *input,
    const BITMAPINFOHEADER *output);
int iv50_decoder_decode(
    Iv50Decoder *decoder,
    const ICDECOMPRESS *request);
void iv50_decoder_end(Iv50Decoder *decoder);

LRESULT CALLBACK DriverProc(
    DWORD_PTR driver_id,
    HDRVR driver,
    UINT message,
    LPARAM param1,
    LPARAM param2);

#ifdef __cplusplus
}
#endif

#endif
