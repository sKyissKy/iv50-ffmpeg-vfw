/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "iv50_vfw.h"

#include <stdio.h>

static int failures;

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            ++failures; \
        } \
    } while (0)

static BITMAPINFOHEADER make_input(void)
{
    BITMAPINFOHEADER input;
    ZeroMemory(&input, sizeof(input));
    input.biSize = sizeof(input);
    input.biWidth = 320;
    input.biHeight = 240;
    input.biPlanes = 1;
    input.biBitCount = 24;
    input.biCompression = IV50_FOURCC;
    input.biSizeImage = 1024;
    return input;
}

int main(void)
{
    ICOPEN open_request;
    BITMAPINFOHEADER input = make_input();
    BITMAPINFOHEADER output;
    ICINFO info;
    DWORD_PTR first;
    DWORD_PTR second;

    ZeroMemory(&open_request, sizeof(open_request));
    open_request.dwSize = sizeof(open_request);
    open_request.fccType = ICTYPE_VIDEO;
    open_request.fccHandler = IV50_FOURCC;

    CHECK(DriverProc(0, NULL, DRV_INSTALL, 0, 0) == DRVCNF_OK);
    CHECK(DriverProc(0, NULL, DRV_REMOVE, 0, 0) == DRVCNF_OK);

    first = (DWORD_PTR)DriverProc(0, NULL, DRV_OPEN, 0, (LPARAM)&open_request);
    second = (DWORD_PTR)DriverProc(0, NULL, DRV_OPEN, 0, (LPARAM)&open_request);
    CHECK(first != 0);
    CHECK(second != 0);
    CHECK(first != second);
    CHECK(open_request.dwError == ICERR_OK);

    CHECK(DriverProc(first, NULL, ICM_GETINFO, (LPARAM)&info, sizeof(info)) == sizeof(info));
    CHECK(info.fccHandler == IV50_FOURCC);
    CHECK((info.dwFlags & VIDCF_TEMPORAL) != 0);

    CHECK(DriverProc(first, NULL, ICM_DECOMPRESS_GET_FORMAT, (LPARAM)&input, 0) == sizeof(output));
    CHECK(DriverProc(first, NULL, ICM_DECOMPRESS_GET_FORMAT, (LPARAM)&input, (LPARAM)&output) == ICERR_OK);
    CHECK(DriverProc(first, NULL, ICM_DECOMPRESS_QUERY, (LPARAM)&input, (LPARAM)&output) == ICERR_OK);
    CHECK(DriverProc(first, NULL, ICM_DECOMPRESS, 0, 0) == ICERR_BADPARAM);
    CHECK(DriverProc(first, NULL, ICM_DECOMPRESS_BEGIN, (LPARAM)&input, (LPARAM)&output) == ICERR_OK);
    CHECK(DriverProc(first, NULL, ICM_DECOMPRESS_END, 0, 0) == ICERR_OK);

    input.biCompression = mmioFOURCC('I', 'V', '4', '1');
    CHECK(DriverProc(first, NULL, ICM_DECOMPRESS_QUERY, (LPARAM)&input, (LPARAM)&output) == ICERR_BADFORMAT);

    CHECK(DriverProc(second, NULL, DRV_CLOSE, 0, 0) == 1);
    CHECK(DriverProc(first, NULL, DRV_CLOSE, 0, 0) == 1);

    open_request.fccHandler = IV50_VCM_HANDLER;
    first = (DWORD_PTR)DriverProc(0, NULL, DRV_OPEN, 0, (LPARAM)&open_request);
    CHECK(first != 0);
    CHECK(DriverProc(first, NULL, DRV_CLOSE, 0, 0) == 1);

    open_request.fccHandler = mmioFOURCC('I', 'V', '4', '1');
    CHECK(DriverProc(0, NULL, DRV_OPEN, 0, (LPARAM)&open_request) == 0);
    CHECK(open_request.dwError == ICERR_BADFORMAT);

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    puts("all driver tests passed");
    return 0;
}
