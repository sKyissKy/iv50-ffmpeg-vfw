/* SPDX-License-Identifier: LGPL-2.1-or-later */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vfw.h>

#include <stdint.h>
#include <stdio.h>

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
    PAVIFILE file = NULL;
    PAVISTREAM stream = NULL;
    PGETFRAME get_frame = NULL;
    AVISTREAMINFOW stream_info;
    HIC first_codec = NULL;
    HIC second_codec = NULL;
    LONG frame;
    LONG first_frame;
    LONG frame_count;
    uint32_t crc = 0;
    HRESULT result;
    int exit_code = 1;

    if (argc != 2) {
        fwprintf(stderr, L"usage: %ls <iv50.avi>\n", argv[0]);
        return 2;
    }

    first_codec = ICOpen(ICTYPE_VIDEO, mmioFOURCC('I', 'V', '5', '0'), ICMODE_DECOMPRESS);
    second_codec = ICOpen(ICTYPE_VIDEO, mmioFOURCC('I', 'V', '5', '0'), ICMODE_DECOMPRESS);
    if (first_codec == NULL || second_codec == NULL) {
        fwprintf(stderr, L"ICOpen(VIDC, IV50) failed\n");
        goto cleanup;
    }

    AVIFileInit();
    result = AVIFileOpenW(&file, argv[1], OF_READ | OF_SHARE_DENY_WRITE, NULL);
    if (FAILED(result)) {
        fwprintf(stderr, L"AVIFileOpenW failed: 0x%08lx\n", (unsigned long)result);
        goto cleanup_avi;
    }
    result = AVIFileGetStream(file, &stream, streamtypeVIDEO, 0);
    if (FAILED(result)) {
        fwprintf(stderr, L"AVIFileGetStream failed: 0x%08lx\n", (unsigned long)result);
        goto cleanup_avi;
    }
    ZeroMemory(&stream_info, sizeof(stream_info));
    result = AVIStreamInfoW(stream, &stream_info, sizeof(stream_info));
    if (FAILED(result) || stream_info.fccHandler != mmioFOURCC('I', 'V', '5', '0')) {
        fwprintf(stderr, L"input stream is not IV50\n");
        goto cleanup_avi;
    }

    get_frame = AVIStreamGetFrameOpen(stream, NULL);
    if (get_frame == NULL) {
        fwprintf(stderr, L"AVIStreamGetFrameOpen failed\n");
        goto cleanup_avi;
    }

    first_frame = AVIStreamStart(stream);
    frame_count = AVIStreamLength(stream);
    for (frame = first_frame; frame < first_frame + frame_count; ++frame) {
        const BITMAPINFOHEADER *header =
            (const BITMAPINFOHEADER *)AVIStreamGetFrame(get_frame, frame);
        DWORD size;
        const uint8_t *pixels;

        if (header == NULL) {
            fwprintf(stderr, L"frame %ld failed\n", frame);
            goto cleanup_avi;
        }
        size = dib_size(header);
        if (size == 0) {
            fwprintf(stderr, L"frame %ld returned unsupported DIB\n", frame);
            goto cleanup_avi;
        }
        pixels = (const uint8_t *)header + header->biSize;
        crc = crc32_update(crc, pixels, size);
    }

    wprintf(L"frames=%ld crc32=%08x\n", frame_count, crc);
    exit_code = 0;

cleanup_avi:
    if (get_frame != NULL) {
        AVIStreamGetFrameClose(get_frame);
    }
    if (stream != NULL) {
        AVIStreamRelease(stream);
    }
    if (file != NULL) {
        AVIFileRelease(file);
    }
    AVIFileExit();
cleanup:
    if (second_codec != NULL) {
        ICClose(second_codec);
    }
    if (first_codec != NULL) {
        ICClose(first_codec);
    }
    return exit_code;
}
