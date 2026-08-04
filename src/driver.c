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
    iv50_decoder_destroy(instance->decoder);
    free(instance);
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
