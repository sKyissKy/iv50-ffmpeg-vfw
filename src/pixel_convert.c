/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "iv50_vfw.h"

#include <limits.h>
#include <string.h>

static BOOL iv50_abs_height(LONG height, uint64_t *absolute_height)
{
    int64_t signed_height;

    if (absolute_height == NULL || height == 0 || height == LONG_MIN) {
        return FALSE;
    }

    signed_height = height;
    if (signed_height < 0) {
        signed_height = -signed_height;
    }

    *absolute_height = (uint64_t)signed_height;
    return TRUE;
}

BOOL iv50_calculate_image_size(
    LONG width,
    LONG height,
    WORD bit_count,
    DWORD *stride,
    DWORD *image_size)
{
    uint64_t absolute_height;
    uint64_t row_bits;
    uint64_t row_bytes;
    uint64_t total_size;

    if (stride == NULL || image_size == NULL || width <= 0) {
        return FALSE;
    }
    if (bit_count != 24 && bit_count != 32) {
        return FALSE;
    }
    if ((uint64_t)width > IV50_MAX_DIMENSION ||
        !iv50_abs_height(height, &absolute_height) ||
        absolute_height > IV50_MAX_DIMENSION) {
        return FALSE;
    }

    row_bits = (uint64_t)(uint32_t)width * bit_count;
    row_bytes = ((row_bits + 31U) / 32U) * 4U;
    total_size = row_bytes * absolute_height;

    if (row_bytes > MAXDWORD || total_size > MAXDWORD) {
        return FALSE;
    }

    *stride = (DWORD)row_bytes;
    *image_size = (DWORD)total_size;
    return TRUE;
}

BOOL iv50_is_input_format_supported(const BITMAPINFOHEADER *input)
{
    uint64_t absolute_height;

    if (input == NULL || input->biSize < sizeof(BITMAPINFOHEADER)) {
        return FALSE;
    }
    if (input->biCompression != IV50_FOURCC || input->biPlanes != 1) {
        return FALSE;
    }
    if (input->biWidth <= 0 ||
        (uint64_t)input->biWidth > IV50_MAX_DIMENSION ||
        !iv50_abs_height(input->biHeight, &absolute_height) ||
        absolute_height > IV50_MAX_DIMENSION) {
        return FALSE;
    }
    if (input->biSize - sizeof(BITMAPINFOHEADER) > IV50_MAX_EXTRADATA) {
        return FALSE;
    }

    return TRUE;
}

BOOL iv50_is_output_format_supported(
    const BITMAPINFOHEADER *input,
    const BITMAPINFOHEADER *output)
{
    DWORD stride;
    DWORD image_size;
    int64_t input_height;
    int64_t output_height;

    if (!iv50_is_input_format_supported(input)) {
        return FALSE;
    }
    if (output == NULL) {
        return TRUE;
    }
    if (output->biSize < sizeof(BITMAPINFOHEADER) ||
        output->biCompression != BI_RGB ||
        output->biPlanes != 1 ||
        (output->biBitCount != 24 && output->biBitCount != 32) ||
        output->biWidth != input->biWidth) {
        return FALSE;
    }

    input_height = input->biHeight;
    output_height = output->biHeight;
    if (input_height < 0) {
        input_height = -input_height;
    }
    if (output_height < 0) {
        output_height = -output_height;
    }
    if (input_height != output_height) {
        return FALSE;
    }

    return iv50_calculate_image_size(
        output->biWidth,
        output->biHeight,
        output->biBitCount,
        &stride,
        &image_size);
}

LRESULT iv50_get_output_format(
    const BITMAPINFOHEADER *input,
    BITMAPINFOHEADER *output)
{
    DWORD stride;
    DWORD image_size;

    if (!iv50_is_input_format_supported(input)) {
        return ICERR_BADFORMAT;
    }
    if (output == NULL) {
        return sizeof(BITMAPINFOHEADER);
    }
    if (!iv50_calculate_image_size(
            input->biWidth,
            input->biHeight,
            24,
            &stride,
            &image_size)) {
        return ICERR_BADFORMAT;
    }

    ZeroMemory(output, sizeof(*output));
    output->biSize = sizeof(*output);
    output->biWidth = input->biWidth;
    output->biHeight = input->biHeight;
    output->biPlanes = 1;
    output->biBitCount = 24;
    output->biCompression = BI_RGB;
    output->biSizeImage = image_size;

    return ICERR_OK;
}
