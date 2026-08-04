/* SPDX-License-Identifier: LGPL-2.1-or-later */

#define INITGUID

#include "iv50_mft.h"

#include <mfapi.h>
#include <mftransform.h>
#include <windows.h>

#include <cstdio>

int wmain()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        wprintf(L"CoInitializeEx failed: 0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        wprintf(L"MFStartup failed: 0x%08lX\n", static_cast<unsigned long>(hr));
        CoUninitialize();
        return 1;
    }

    MFT_REGISTER_TYPE_INFO input = { MFMediaType_Video, MFVideoFormat_IV50 };
    IMFActivate **activates = nullptr;
    UINT32 count = 0;
    hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, MFT_ENUM_FLAG_ALL,
        &input, nullptr, &activates, &count);
    wprintf(L"MFTEnumEx hr=0x%08lX count=%u\n",
        static_cast<unsigned long>(hr), count);
    bool found = false;
    if (SUCCEEDED(hr)) {
        for (UINT32 i = 0; i < count; ++i) {
            GUID clsid = {};
            HRESULT item_hr = activates[i]->GetGUID(MFT_TRANSFORM_CLSID_Attribute, &clsid);
            WCHAR text[64] = {};
            StringFromGUID2(clsid, text, ARRAYSIZE(text));
            wprintf(L"[%u] clsid=%s attribute_hr=0x%08lX\n", i, text,
                static_cast<unsigned long>(item_hr));
            if (clsid == CLSID_IV50_FFMPEG_MFT) found = true;
            activates[i]->Release();
        }
    }
    CoTaskMemFree(activates);

    IMFTransform *transform = nullptr;
    HRESULT create_hr = CoCreateInstance(CLSID_IV50_FFMPEG_MFT, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&transform));
    wprintf(L"CoCreateInstance hr=0x%08lX\n", static_cast<unsigned long>(create_hr));
    if (transform) transform->Release();

    MFShutdown();
    CoUninitialize();
    return found && SUCCEEDED(create_hr) ? 0 : 2;
}
