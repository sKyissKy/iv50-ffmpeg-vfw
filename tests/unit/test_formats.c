/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "iv50_vfw.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            ++failures; \
        } \
    } while (0)

static BITMAPINFOHEADER make_iv50_input(LONG width, LONG height)
{
    BITMAPINFOHEADER input;

    ZeroMemory(&input, sizeof(input));
    input.biSize = sizeof(input);
    input.biWidth = width;
    input.biHeight = height;
    input.biPlanes = 1;
    input.biBitCount = 24;
    input.biCompression = IV50_FOURCC;
    input.biSizeImage = 4096;
    return input;
}

static void test_fourcc_and_dimensions(void)
{
    BITMAPINFOHEADER input = make_iv50_input(320, 240);

    CHECK(iv50_is_input_format_supported(&input));
    input.biCompression = mmioFOURCC('I', 'V', '4', '1');
    CHECK(!iv50_is_input_format_supported(&input));
    input = make_iv50_input(0, 240);
    CHECK(!iv50_is_input_format_supported(&input));
    input = make_iv50_input(320, 0);
    CHECK(!iv50_is_input_format_supported(&input));
    input = make_iv50_input(IV50_MAX_DIMENSION + 1, 240);
    CHECK(!iv50_is_input_format_supported(&input));
}

static void test_stride_and_size(void)
{
    DWORD stride = 0;
    DWORD image_size = 0;

    CHECK(iv50_calculate_image_size(1, 1, 24, &stride, &image_size));
    CHECK(stride == 4);
    CHECK(image_size == 4);
    CHECK(iv50_calculate_image_size(3, -2, 24, &stride, &image_size));
    CHECK(stride == 12);
    CHECK(image_size == 24);
    CHECK(iv50_calculate_image_size(3, 2, 32, &stride, &image_size));
    CHECK(stride == 12);
    CHECK(image_size == 24);
    CHECK(!iv50_calculate_image_size(3, 2, 16, &stride, &image_size));
}

static void test_output_formats(void)
{
    BITMAPINFOHEADER input = make_iv50_input(321, 241);
    BITMAPINFOHEADER output;
    LRESULT result;

    CHECK(iv50_get_output_format(&input, NULL) == sizeof(BITMAPINFOHEADER));
    result = iv50_get_output_format(&input, &output);
    CHECK(result == ICERR_OK);
    CHECK(output.biCompression == BI_RGB);
    CHECK(output.biBitCount == 24);
    CHECK(output.biSizeImage == 232324);
    CHECK(iv50_is_output_format_supported(&input, &output));

    output.biBitCount = 32;
    output.biSizeImage = 0;
    CHECK(iv50_is_output_format_supported(&input, &output));
    output.biWidth++;
    CHECK(!iv50_is_output_format_supported(&input, &output));

    input = make_iv50_input(320, -240);
    result = iv50_get_output_format(&input, &output);
    CHECK(result == ICERR_OK);
    CHECK(output.biHeight == -240);
}

int main(void)
{
    test_fourcc_and_dimensions();
    test_stride_and_size();
    test_output_formats();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    puts("all format tests passed");
    return 0;
}
