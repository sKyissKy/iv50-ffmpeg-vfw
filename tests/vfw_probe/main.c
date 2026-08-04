/* SPDX-License-Identifier: LGPL-2.1-or-later */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vfw.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef LRESULT(CALLBACK *CodecDriverProc)(
    DWORD_PTR driver_id,
    HDRVR driver,
    UINT message,
    LPARAM param1,
    LPARAM param2);

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
    size_t index;
    unsigned int bit;

    crc = ~crc;
    for (index = 0; index < size; ++index) {
        crc ^= data[index];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

static DWORD dib_size(const BITMAPINFOHEADER *header)
{
    uint64_t height;
    uint64_t stride;
    uint64_t size;

    if (header == NULL || header->biWidth <= 0 || header->biHeight == 0 ||
        (header->biBitCount != 24 && header->biBitCount != 32)) {
        return 0;
    }
    height = header->biHeight < 0
        ? (uint64_t)(-(int64_t)header->biHeight)
        : (uint64_t)header->biHeight;
    stride = ((((uint64_t)header->biWidth * header->biBitCount) + 31U) / 32U) * 4U;
    size = stride * height;
    return size <= MAXDWORD ? (DWORD)size : 0;
}

int wmain(int argc, wchar_t **argv)
{
    HMODULE codec_module = NULL;
    CodecDriverProc codec_proc = NULL;
    HIC first_codec = NULL;
    HIC second_codec = NULL;
    PAVIFILE file = NULL;
    PAVISTREAM stream = NULL;
    AVISTREAMINFOW stream_info;
    BITMAPINFOHEADER *input_format = NULL;
    BITMAPINFOHEADER *output_format = NULL;
    uint8_t *compressed = NULL;
    uint8_t *pixels = NULL;
    LONG format_size = 0;
    LONG output_format_size;
    LONG compressed_capacity = 0;
    LONG first_frame;
    LONG frame_count;
    LONG frame_offset;
    DWORD pixel_size;
    uint32_t crc = 0;
    HRESULT result;
    BOOL avi_initialized = FALSE;
    BOOL decompress_started = FALSE;
    BOOL registry_mode = FALSE;
    int exit_code = 1;

    if (argc != 3) {
        fwprintf(stderr, L"usage: %ls <codec.dll|--registry> <iv50.avi>\n", argv[0]);
        return 2;
    }

    registry_mode = _wcsicmp(argv[1], L"--registry") == 0;
    if (!registry_mode) {
        codec_module = LoadLibraryW(argv[1]);
        if (codec_module == NULL) {
            fwprintf(stderr, L"LoadLibraryW failed: %lu\n", GetLastError());
            goto cleanup;
        }
        codec_proc = (CodecDriverProc)(void *)GetProcAddress(codec_module, "DriverProc");
        if (codec_proc == NULL) {
            fwprintf(stderr, L"DriverProc export not found\n");
            goto cleanup;
        }
    }
    if (registry_mode) {
        first_codec = ICOpen(
            ICTYPE_VIDEO, mmioFOURCC('I', 'V', '5', '0'), ICMODE_DECOMPRESS);
        second_codec = ICOpen(
            ICTYPE_VIDEO, mmioFOURCC('I', 'V', '5', '0'), ICMODE_DECOMPRESS);
    } else {
        first_codec = ICOpenFunction(
            ICTYPE_VIDEO,
            mmioFOURCC('I', 'V', '5', '0'),
            ICMODE_DECOMPRESS,
            (FARPROC)codec_proc);
        second_codec = ICOpenFunction(
            ICTYPE_VIDEO,
            mmioFOURCC('I', 'V', '5', '0'),
            ICMODE_DECOMPRESS,
            (FARPROC)codec_proc);
    }
    if (first_codec == NULL || second_codec == NULL) {
        fwprintf(stderr, L"ICOpenFunction(VIDC, IV50) failed\n");
        goto cleanup;
    }

    AVIFileInit();
    avi_initialized = TRUE;
    result = AVIFileOpenW(&file, argv[2], OF_READ | OF_SHARE_DENY_WRITE, NULL);
    if (FAILED(result)) {
        fwprintf(stderr, L"AVIFileOpenW failed: 0x%08lx\n", (unsigned long)result);
        goto cleanup;
    }
    result = AVIFileGetStream(file, &stream, streamtypeVIDEO, 0);
    if (FAILED(result)) {
        fwprintf(stderr, L"AVIFileGetStream failed: 0x%08lx\n", (unsigned long)result);
        goto cleanup;
    }
    ZeroMemory(&stream_info, sizeof(stream_info));
    result = AVIStreamInfoW(stream, &stream_info, sizeof(stream_info));
    if (FAILED(result) || stream_info.fccHandler != mmioFOURCC('I', 'V', '5', '0')) {
        fwprintf(stderr, L"input stream is not IV50\n");
        goto cleanup;
    }

    first_frame = AVIStreamStart(stream);
    frame_count = AVIStreamLength(stream);
    if (first_frame < 0 || frame_count <= 0 || frame_count > LONG_MAX - first_frame) {
        fwprintf(stderr, L"invalid AVI frame range\n");
        goto cleanup;
    }
    result = AVIStreamReadFormat(stream, first_frame, NULL, &format_size);
    if (FAILED(result) || format_size < (LONG)sizeof(BITMAPINFOHEADER)) {
        fwprintf(stderr, L"AVIStreamReadFormat(size) failed\n");
        goto cleanup;
    }
    input_format = (BITMAPINFOHEADER *)calloc(1, (size_t)format_size);
    if (input_format == NULL) {
        fwprintf(stderr, L"out of memory for input format\n");
        goto cleanup;
    }
    result = AVIStreamReadFormat(stream, first_frame, input_format, &format_size);
    if (FAILED(result) || input_format->biSize < sizeof(BITMAPINFOHEADER) ||
        input_format->biCompression != mmioFOURCC('I', 'V', '5', '0')) {
        fwprintf(stderr, L"invalid IV50 stream format\n");
        goto cleanup;
    }

    output_format_size = ICDecompressGetFormatSize(first_codec, input_format);
    if (output_format_size < (LONG)sizeof(BITMAPINFOHEADER)) {
        fwprintf(stderr, L"ICM_DECOMPRESS_GET_FORMAT(size) failed: %ld\n", output_format_size);
        goto cleanup;
    }
    output_format = (BITMAPINFOHEADER *)calloc(1, (size_t)output_format_size);
    if (output_format == NULL) {
        fwprintf(stderr, L"out of memory for output format\n");
        goto cleanup;
    }
    if (ICDecompressGetFormat(first_codec, input_format, output_format) != ICERR_OK) {
        fwprintf(stderr, L"ICM_DECOMPRESS_GET_FORMAT failed\n");
        goto cleanup;
    }
    pixel_size = dib_size(output_format);
    if (pixel_size == 0 || output_format->biSizeImage < pixel_size) {
        fwprintf(stderr, L"codec returned an invalid output DIB\n");
        goto cleanup;
    }
    pixels = (uint8_t *)malloc(pixel_size);
    if (pixels == NULL) {
        fwprintf(stderr, L"out of memory for output frame\n");
        goto cleanup;
    }
    if (ICDecompressBegin(first_codec, input_format, output_format) != ICERR_OK) {
        fwprintf(stderr, L"ICM_DECOMPRESS_BEGIN failed\n");
        goto cleanup;
    }
    decompress_started = TRUE;

    for (frame_offset = 0; frame_offset < frame_count; ++frame_offset) {
        LONG frame = first_frame + frame_offset;
        LONG sample_size = 0;
        LONG bytes_read = 0;
        LONG samples_read = 0;
        LONG key_frame;
        ICDECOMPRESS request;
        LRESULT decode_result;

        result = AVIStreamSampleSize(stream, frame, &sample_size);
        if (FAILED(result) || sample_size <= 0) {
            fwprintf(stderr, L"frame %ld has an invalid sample size\n", frame);
            goto cleanup;
        }
        if (sample_size > compressed_capacity) {
            uint8_t *larger = (uint8_t *)realloc(compressed, (size_t)sample_size);
            if (larger == NULL) {
                fwprintf(stderr, L"out of memory for frame %ld\n", frame);
                goto cleanup;
            }
            compressed = larger;
            compressed_capacity = sample_size;
        }
        result = AVIStreamRead(
            stream,
            frame,
            1,
            compressed,
            compressed_capacity,
            &bytes_read,
            &samples_read);
        if (FAILED(result) || bytes_read <= 0 || samples_read != 1) {
            fwprintf(stderr, L"AVIStreamRead failed for frame %ld\n", frame);
            goto cleanup;
        }
        ZeroMemory(&request, sizeof(request));
        input_format->biSizeImage = (DWORD)bytes_read;
        request.lpbiInput = input_format;
        request.lpInput = compressed;
        request.lpbiOutput = output_format;
        request.lpOutput = pixels;
        key_frame = AVIStreamFindSample(stream, frame, FIND_KEY | FIND_PREV);
        if (key_frame != frame) {
            request.dwFlags |= ICDECOMPRESS_NOTKEYFRAME;
        }
        decode_result = ICSendMessage(
            first_codec,
            ICM_DECOMPRESS,
            (DWORD_PTR)&request,
            sizeof(request));
        if (decode_result != ICERR_OK) {
            fwprintf(
                stderr,
                L"frame %ld decode failed: %lld\n",
                frame,
                (long long)decode_result);
            goto cleanup;
        }
        crc = crc32_update(crc, pixels, pixel_size);
    }

    wprintf(L"frames=%ld crc32=%08x\n", frame_count, crc);
    exit_code = 0;

cleanup:
    if (decompress_started) {
        ICDecompressEnd(first_codec);
    }
    free(pixels);
    free(compressed);
    free(output_format);
    free(input_format);
    if (stream != NULL) {
        AVIStreamRelease(stream);
    }
    if (file != NULL) {
        AVIFileRelease(file);
    }
    if (avi_initialized) {
        AVIFileExit();
    }
    if (second_codec != NULL) {
        ICClose(second_codec);
    }
    if (first_codec != NULL) {
        ICClose(first_codec);
    }
    if (codec_module != NULL) {
        FreeLibrary(codec_module);
    }
    return exit_code;
}
