/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "iv50_vfw.h"

#include <stdlib.h>
#include <string.h>

static Iv50VfwInstance *iv50_instance_create(ICOPEN *open_request)
{
    Iv50VfwInstance *instance;

    if (open_request != NULL) {
        if (open_request->fccType != ICTYPE_VIDEO) {
            open_request->dwError = ICERR_BADFORMAT;
            return NULL;
        }
        if (open_request->fccHandler != 0 &&
            open_request->fccHandler != IV50_FOURCC &&
            open_request->fccHandler != IV50_VCM_HANDLER) {
            open_request->dwError = ICERR_BADFORMAT;
            return NULL;
        }
    }

    instance = (Iv50VfwInstance *)calloc(1, sizeof(*instance));
    if (instance == NULL) {
        if (open_request != NULL) {
            open_request->dwError = ICERR_MEMORY;
        }
        return NULL;
    }
    instance->decoder = iv50_decoder_create();
    if (instance->decoder == NULL) {
        free(instance);
        if (open_request != NULL) {
            open_request->dwError = ICERR_MEMORY;
        }
        return NULL;
    }
    if (open_request != NULL) {
        open_request->dwError = ICERR_OK;
    }
    return instance;
}

static void iv50_instance_destroy(Iv50VfwInstance *instance)
{
    if (instance == NULL) {
        return;
    }
    free(instance->draw_buffer);
    iv50_decoder_destroy(instance->decoder);
    free(instance);
}

static void iv50_draw_end(Iv50VfwInstance *instance)
{
    if (instance == NULL) {
        return;
    }
    free(instance->draw_buffer);
    instance->draw_buffer = NULL;
    instance->draw_buffer_size = 0;
    if (instance->begun) {
        iv50_decoder_end(instance->decoder);
        instance->begun = FALSE;
    }
    ZeroMemory(&instance->draw_begin, sizeof(instance->draw_begin));
    instance->draw_begun = FALSE;
}

static LRESULT iv50_draw_begin(
    Iv50VfwInstance *instance,
    const ICDRAWBEGIN *request)
{
    BITMAPINFOHEADER output;
    DWORD stride;
    DWORD image_size;
    int result;

    if (instance == NULL || request == NULL || request->lpbi == NULL) {
        return ICERR_BADPARAM;
    }
    if ((request->dwFlags & ICDRAW_HDC) == 0 || request->hdc == NULL ||
        !iv50_is_input_format_supported(request->lpbi)) {
        return ICERR_UNSUPPORTED;
    }

    iv50_draw_end(instance);
    if (iv50_get_output_format(request->lpbi, &output) != ICERR_OK ||
        !iv50_calculate_image_size(
            output.biWidth, output.biHeight, output.biBitCount,
            &stride, &image_size)) {
        return ICERR_BADFORMAT;
    }
    result = iv50_decoder_begin(instance->decoder, request->lpbi, &output);
    if (result != ICERR_OK) {
        return result;
    }
    instance->input_format = *request->lpbi;
    instance->output_format = output;
    instance->draw_begin = *request;
    instance->draw_buffer = (uint8_t *)malloc(image_size);
    if (instance->draw_buffer == NULL) {
        iv50_draw_end(instance);
        return ICERR_MEMORY;
    }
    instance->draw_buffer_size = image_size;
    instance->begun = TRUE;
    instance->draw_begun = TRUE;
    return ICERR_OK;
}

static LRESULT iv50_draw_frame(
    Iv50VfwInstance *instance,
    const ICDRAW *request)
{
    BITMAPINFOHEADER input;
    ICDECOMPRESS decompress;
    int result;
    int source_width;
    int source_height;

    if (instance == NULL || !instance->draw_begun || request == NULL ||
        instance->draw_buffer == NULL) {
        return ICERR_BADPARAM;
    }
    if ((request->dwFlags & ICDRAW_NULLFRAME) != 0) {
        return ICERR_OK;
    }
    if (request->lpData == NULL || request->cbData == 0 ||
        request->cbData > IV50_MAX_COMPRESSED_FRAME) {
        return ICERR_BADPARAM;
    }

    input = instance->input_format;
    input.biSizeImage = request->cbData;
    ZeroMemory(&decompress, sizeof(decompress));
    decompress.dwFlags = (request->dwFlags & ICDRAW_NOTKEYFRAME)
        ? ICDECOMPRESS_NOTKEYFRAME
        : 0;
    decompress.lpbiInput = &input;
    decompress.lpbiOutput = &instance->output_format;
    decompress.lpInput = request->lpData;
    decompress.lpOutput = instance->draw_buffer;
    result = iv50_decoder_decode(instance->decoder, &decompress);
    if (result != ICERR_OK) {
        return result;
    }

    source_width = instance->output_format.biWidth;
    source_height = instance->output_format.biHeight < 0
        ? -instance->output_format.biHeight
        : instance->output_format.biHeight;
    if (StretchDIBits(
            instance->draw_begin.hdc,
            instance->draw_begin.xDst,
            instance->draw_begin.yDst,
            instance->draw_begin.dxDst,
            instance->draw_begin.dyDst,
            instance->draw_begin.xSrc,
            instance->draw_begin.ySrc,
            instance->draw_begin.dxSrc != 0
                ? instance->draw_begin.dxSrc : source_width,
            instance->draw_begin.dySrc != 0
                ? instance->draw_begin.dySrc : source_height,
            instance->draw_buffer,
            (const BITMAPINFO *)&instance->output_format,
            DIB_RGB_COLORS,
            SRCCOPY) == 0) {
        return ICERR_ERROR;
    }
    return ICERR_OK;
}

static LRESULT iv50_get_info(ICINFO *info, DWORD size)
{
    if (info == NULL || size < sizeof(*info)) {
        return 0;
    }

    ZeroMemory(info, sizeof(*info));
    info->dwSize = sizeof(*info);
    info->fccType = ICTYPE_VIDEO;
    info->fccHandler = IV50_FOURCC;
    info->dwFlags = VIDCF_TEMPORAL | VIDCF_FASTTEMPORALD;
    info->dwVersion = MAKELONG(0, 1);
    info->dwVersionICM = ICVERSION;
    lstrcpynW(info->szName, L"FFmpeg IV50 VFW Decoder", ARRAYSIZE(info->szName));
    lstrcpynW(
        info->szDescription,
        L"Intel Indeo Video 5 decoder powered by FFmpeg",
        ARRAYSIZE(info->szDescription));
    return sizeof(*info);
}

static LRESULT iv50_decompress_begin(
    Iv50VfwInstance *instance,
    const BITMAPINFOHEADER *input,
    const BITMAPINFOHEADER *output)
{
    int result;

    if (instance == NULL) {
        return ICERR_BADPARAM;
    }
    result = iv50_decoder_begin(instance->decoder, input, output);
    if (result == ICERR_OK) {
        instance->input_format = *input;
        instance->output_format = *output;
        instance->begun = TRUE;
    }
    return result;
}

static LRESULT iv50_driver_proc_impl(
    DWORD_PTR driver_id,
    HDRVR driver,
    UINT message,
    LPARAM param1,
    LPARAM param2)
{
    Iv50VfwInstance *instance = (Iv50VfwInstance *)driver_id;

    switch (message) {
    case DRV_LOAD:
    case DRV_FREE:
    case DRV_ENABLE:
    case DRV_DISABLE:
        return 1;

    case DRV_INSTALL:
    case DRV_REMOVE:
        return DRVCNF_OK;

    case DRV_OPEN:
        return (LRESULT)(DWORD_PTR)iv50_instance_create((ICOPEN *)param2);

    case DRV_CLOSE:
        iv50_instance_destroy(instance);
        return 1;

    case DRV_QUERYCONFIGURE:
        return 0;

    case DRV_CONFIGURE:
        return DRVCNF_CANCEL;

    case ICM_GETINFO:
        return iv50_get_info((ICINFO *)param1, (DWORD)param2);

    case ICM_DRAW_QUERY:
        return ICERR_OK;

    case ICM_DRAW_BEGIN:
        return iv50_draw_begin(instance, (const ICDRAWBEGIN *)param1);

    case ICM_DRAW:
        return iv50_draw_frame(instance, (const ICDRAW *)param1);

    case ICM_DRAW_END:
        if (instance == NULL) {
            return ICERR_BADPARAM;
        }
        iv50_draw_end(instance);
        return ICERR_OK;

    case ICM_DRAW_START:
    case ICM_DRAW_STOP:
    case ICM_DRAW_START_PLAY:
    case ICM_DRAW_STOP_PLAY:
    case ICM_DRAW_WINDOW:
    case ICM_DRAW_REALIZE:
    case ICM_DRAW_FLUSH:
    case ICM_DRAW_RENDERBUFFER:
    case ICM_DRAW_UPDATE:
    case ICM_GETBUFFERSWANTED:
        return ICERR_OK;

    case ICM_DECOMPRESS_QUERY:
        return iv50_is_output_format_supported(
            (const BITMAPINFOHEADER *)param1,
            (const BITMAPINFOHEADER *)param2)
            ? ICERR_OK
            : ICERR_BADFORMAT;

    case ICM_DECOMPRESS_GET_FORMAT:
        return iv50_get_output_format(
            (const BITMAPINFOHEADER *)param1,
            (BITMAPINFOHEADER *)param2);

    case ICM_DECOMPRESS_BEGIN:
        return iv50_decompress_begin(
            instance,
            (const BITMAPINFOHEADER *)param1,
            (const BITMAPINFOHEADER *)param2);

    case ICM_DECOMPRESS:
        if (instance == NULL || !instance->begun) {
            return ICERR_BADPARAM;
        }
        return iv50_decoder_decode(instance->decoder, (const ICDECOMPRESS *)param1);

    case ICM_DECOMPRESS_END:
        if (instance == NULL) {
            return ICERR_BADPARAM;
        }
        iv50_decoder_end(instance->decoder);
        instance->begun = FALSE;
        return ICERR_OK;

    case ICM_ABOUT:
    case ICM_CONFIGURE:
        return ICERR_UNSUPPORTED;

    default:
        return DefDriverProc(driver_id, driver, message, param1, param2);
    }
}

LRESULT CALLBACK DriverProc(
    DWORD_PTR driver_id,
    HDRVR driver,
    UINT message,
    LPARAM param1,
    LPARAM param2)
{
    __try {
        return iv50_driver_proc_impl(
            driver_id,
            driver,
            message,
            param1,
            param2);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return message < DRV_USER ? 0 : ICERR_ERROR;
    }
}
