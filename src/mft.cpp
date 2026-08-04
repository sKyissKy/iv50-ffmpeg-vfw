/* SPDX-License-Identifier: LGPL-2.1-or-later */

#define INITGUID

#include "iv50_mft.h"
#include "iv50_vfw.h"

#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <windows.h>

#include <algorithm>
#include <new>
#include <vector>

static HMODULE g_module;
static volatile long g_object_count;
static volatile long g_lock_count;

static HRESULT CheckStream(DWORD stream)
{
    return stream == 0 ? S_OK : MF_E_INVALIDSTREAMNUMBER;
}

static HRESULT CopyType(IMFMediaType *source, IMFMediaType **target)
{
    if (target == nullptr) {
        return E_POINTER;
    }
    *target = nullptr;
    if (source == nullptr) {
        return S_OK;
    }
    return source->QueryInterface(IID_PPV_ARGS(target));
}

class Iv50Mft final : public IMFTransform {
public:
    Iv50Mft() : ref_count_(1), decoder_(iv50_decoder_create()), input_type_(nullptr),
        output_type_(nullptr), pending_(nullptr), width_(0), height_(0),
        output_size_(0), begun_(FALSE)
    {
        InterlockedIncrement(&g_object_count);
    }

    ~Iv50Mft()
    {
        ResetDecoder();
        if (input_type_) input_type_->Release();
        if (output_type_) output_type_->Release();
        if (pending_) pending_->Release();
        iv50_decoder_destroy(decoder_);
        InterlockedDecrement(&g_object_count);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override
    {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IMFTransform) {
            *object = static_cast<IMFTransform *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        long value = InterlockedDecrement(&ref_count_);
        if (value == 0) delete this;
        return static_cast<ULONG>(value);
    }

    HRESULT STDMETHODCALLTYPE GetStreamLimits(DWORD *in_min, DWORD *in_max,
        DWORD *out_min, DWORD *out_max) override
    {
        if (!in_min || !in_max || !out_min || !out_max) return E_POINTER;
        *in_min = *in_max = *out_min = *out_max = 1;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetStreamCount(DWORD *in_count, DWORD *out_count) override
    {
        if (!in_count || !out_count) return E_POINTER;
        *in_count = *out_count = 1;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetStreamIDs(DWORD, DWORD *, DWORD, DWORD *) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetInputStreamInfo(DWORD stream, MFT_INPUT_STREAM_INFO *info) override
    {
        if (!info) return E_POINTER;
        HRESULT hr = CheckStream(stream);
        if (FAILED(hr)) return hr;
        ZeroMemory(info, sizeof(*info));
        info->dwFlags = MFT_INPUT_STREAM_WHOLE_SAMPLES | MFT_INPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER;
        info->cbMaxLookahead = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetOutputStreamInfo(DWORD stream, MFT_OUTPUT_STREAM_INFO *info) override
    {
        if (!info) return E_POINTER;
        HRESULT hr = CheckStream(stream);
        if (FAILED(hr)) return hr;
        ZeroMemory(info, sizeof(*info));
        info->dwFlags = MFT_OUTPUT_STREAM_WHOLE_SAMPLES |
            MFT_OUTPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER |
            MFT_OUTPUT_STREAM_PROVIDES_SAMPLES;
        info->cbSize = output_size_;
        info->cbAlignment = 4;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetAttributes(IMFAttributes **attributes) override
    {
        if (!attributes) return E_POINTER;
        *attributes = nullptr;
        return MFCreateAttributes(attributes, 1);
    }

    HRESULT STDMETHODCALLTYPE GetInputStreamAttributes(DWORD stream, IMFAttributes **attributes) override
    {
        HRESULT hr = CheckStream(stream);
        if (FAILED(hr)) return hr;
        return GetAttributes(attributes);
    }

    HRESULT STDMETHODCALLTYPE GetOutputStreamAttributes(DWORD stream, IMFAttributes **attributes) override
    {
        HRESULT hr = CheckStream(stream);
        if (FAILED(hr)) return hr;
        return GetAttributes(attributes);
    }

    HRESULT STDMETHODCALLTYPE DeleteInputStream(DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE AddInputStreams(DWORD, DWORD *) override { return E_NOTIMPL; }

    HRESULT STDMETHODCALLTYPE GetInputAvailableType(DWORD stream, DWORD index, IMFMediaType **type) override
    {
        if (!type) return E_POINTER;
        *type = nullptr;
        HRESULT hr = CheckStream(stream);
        if (FAILED(hr)) return hr;
        if (index != 0) return MF_E_NO_MORE_TYPES;
        hr = MFCreateMediaType(type);
        if (SUCCEEDED(hr)) {
            (*type)->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            (*type)->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_IV50);
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE GetOutputAvailableType(DWORD stream, DWORD index, IMFMediaType **type) override
    {
        if (!type) return E_POINTER;
        *type = nullptr;
        HRESULT hr = CheckStream(stream);
        if (FAILED(hr)) return hr;
        if (index != 0 || width_ <= 0 || height_ <= 0) return MF_E_NO_MORE_TYPES;
        hr = MFCreateMediaType(type);
        if (SUCCEEDED(hr)) {
            (*type)->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            (*type)->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            MFSetAttributeSize(*type, MF_MT_FRAME_SIZE, width_, height_);
            (*type)->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(width_ * 4));
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE SetInputType(DWORD stream, IMFMediaType *type, DWORD flags) override
    {
        HRESULT hr = CheckStream(stream);
        if (FAILED(hr)) return hr;
        if (flags & MFT_SET_TYPE_TEST_ONLY) return ValidateInputType(type);
        hr = ValidateInputType(type);
        if (FAILED(hr)) return hr;
        IMFMediaType *copy = nullptr;
        hr = CopyType(type, &copy);
        if (FAILED(hr)) return hr;
        UINT32 width = 0, height = 0;
        hr = MFGetAttributeSize(copy, MF_MT_FRAME_SIZE, &width, &height);
        if (FAILED(hr)) { copy->Release(); return hr; }
        UINT32 user_data_size = 0;
        if (SUCCEEDED(copy->GetBlobSize(MF_MT_USER_DATA, &user_data_size)) && user_data_size != 0) {
            if (user_data_size > MAXDWORD - sizeof(BITMAPINFOHEADER)) {
                copy->Release();
                return MF_E_INVALIDMEDIATYPE;
            }
        }
        std::vector<BYTE> input_format;
        try {
            input_format.resize(sizeof(BITMAPINFOHEADER) + user_data_size);
        } catch (const std::bad_alloc &) {
            copy->Release();
            return E_OUTOFMEMORY;
        }
        auto *input_header = reinterpret_cast<BITMAPINFOHEADER *>(input_format.data());
        ZeroMemory(input_header, sizeof(*input_header));
        input_header->biSize = static_cast<DWORD>(input_format.size());
        input_header->biWidth = static_cast<LONG>(width);
        input_header->biHeight = static_cast<LONG>(height);
        input_header->biPlanes = 1;
        input_header->biCompression = IV50_FOURCC;
        if (user_data_size != 0) {
            hr = copy->GetBlob(MF_MT_USER_DATA,
                input_format.data() + sizeof(BITMAPINFOHEADER), user_data_size, nullptr);
            if (FAILED(hr)) { copy->Release(); return hr; }
        }
        ResetDecoder();
        if (input_type_) input_type_->Release();
        input_type_ = copy;
        input_format_ = std::move(input_format);
        width_ = static_cast<LONG>(width);
        height_ = static_cast<LONG>(height);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetOutputType(DWORD stream, IMFMediaType *type, DWORD flags) override
    {
        HRESULT hr = CheckStream(stream);
        if (FAILED(hr)) return hr;
        if (!type) return E_POINTER;
        GUID major = {}, subtype = {};
        if (FAILED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) ||
            FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
            major != MFMediaType_Video || subtype != MFVideoFormat_RGB32) {
            return MF_E_INVALIDMEDIATYPE;
        }
        UINT32 width = 0, height = 0;
        if (FAILED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height)) ||
            width != static_cast<UINT32>(width_) || height != static_cast<UINT32>(height_)) {
            return MF_E_INVALIDMEDIATYPE;
        }
        if (flags & MFT_SET_TYPE_TEST_ONLY) return S_OK;
        IMFMediaType *copy = nullptr;
        hr = CopyType(type, &copy);
        if (FAILED(hr)) return hr;
        ResetDecoder();
        if (output_type_) output_type_->Release();
        output_type_ = copy;
        output_size_ = static_cast<DWORD>(width * height * 4U);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetInputCurrentType(DWORD stream, IMFMediaType **type) override
    {
        HRESULT hr = CheckStream(stream);
        if (FAILED(hr)) return hr;
        if (!type) return E_POINTER;
        return CopyType(input_type_, type);
    }

    HRESULT STDMETHODCALLTYPE GetOutputCurrentType(DWORD stream, IMFMediaType **type) override
    {
        HRESULT hr = CheckStream(stream);
        if (FAILED(hr)) return hr;
        if (!type) return E_POINTER;
        return CopyType(output_type_, type);
    }

    HRESULT STDMETHODCALLTYPE GetInputStatus(DWORD stream, DWORD *flags) override
    {
        HRESULT hr = CheckStream(stream);
        if (FAILED(hr)) return hr;
        if (!flags) return E_POINTER;
        *flags = pending_ ? 0 : MFT_INPUT_STATUS_ACCEPT_DATA;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetOutputStatus(DWORD *flags) override
    {
        if (!flags) return E_POINTER;
        *flags = pending_ ? MFT_OUTPUT_STATUS_SAMPLE_READY : 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetOutputBounds(LONGLONG, LONGLONG) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ProcessEvent(DWORD, IMFMediaEvent *) override { return E_NOTIMPL; }

    HRESULT STDMETHODCALLTYPE ProcessMessage(MFT_MESSAGE_TYPE message, ULONG_PTR) override
    {
        if (message == MFT_MESSAGE_COMMAND_FLUSH || message == MFT_MESSAGE_NOTIFY_END_OF_STREAM) {
            if (pending_) { pending_->Release(); pending_ = nullptr; }
            ResetDecoder();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ProcessInput(DWORD stream, IMFSample *sample, DWORD flags) override
    {
        HRESULT hr = CheckStream(stream);
        if (FAILED(hr)) return hr;
        if (!sample || flags != 0) return E_INVALIDARG;
        if (!input_type_ || !output_type_) return MF_E_NOT_INITIALIZED;
        if (pending_) return MF_E_NOTACCEPTING;
        pending_ = sample;
        pending_->AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ProcessOutput(DWORD flags, DWORD count,
        MFT_OUTPUT_DATA_BUFFER *outputs, DWORD *status) override
    {
        if (flags != 0 || count != 1 || !outputs || !status) return E_INVALIDARG;
        *status = 0;
        if (!pending_) return MF_E_TRANSFORM_NEED_MORE_INPUT;
        if (!begun_) {
            HRESULT hr = BeginDecoder();
            if (FAILED(hr)) return hr;
        }
        IMFMediaBuffer *in_buffer = nullptr;
        BYTE *in_data = nullptr;
        DWORD max_length = 0, current_length = 0;
        HRESULT hr = pending_->ConvertToContiguousBuffer(&in_buffer);
        if (FAILED(hr)) return hr;
        hr = in_buffer->Lock(&in_data, &max_length, &current_length);
        if (FAILED(hr)) { in_buffer->Release(); return hr; }

        IMFMediaBuffer *out_buffer = nullptr;
        IMFSample *out_sample = nullptr;
        hr = MFCreateMemoryBuffer(output_size_, &out_buffer);
        if (SUCCEEDED(hr)) hr = MFCreateSample(&out_sample);
        if (SUCCEEDED(hr)) hr = out_sample->AddBuffer(out_buffer);
        if (SUCCEEDED(hr)) {
            BITMAPINFOHEADER output = {};
            auto *input = reinterpret_cast<BITMAPINFOHEADER *>(input_format_.data());
            input->biSizeImage = current_length;
            output.biSize = sizeof(output); output.biWidth = width_; output.biHeight = -height_;
            output.biPlanes = 1; output.biBitCount = 32; output.biCompression = BI_RGB;
            output.biSizeImage = output_size_;
            ICDECOMPRESS request = {};
            request.lpbiInput = input; request.lpbiOutput = &output;
            request.lpInput = in_data;
            BYTE *out_data = nullptr; DWORD out_max = 0, out_current = 0;
            hr = out_buffer->Lock(&out_data, &out_max, &out_current);
            if (SUCCEEDED(hr)) {
                request.lpOutput = out_data;
                hr = iv50_decoder_decode(decoder_, &request) == ICERR_OK ? S_OK : MF_E_TRANSFORM_STREAM_CHANGE;
                out_buffer->Unlock();
                if (SUCCEEDED(hr)) hr = out_buffer->SetCurrentLength(output_size_);
            }
        }
        if (in_data) in_buffer->Unlock();
        in_buffer->Release();
        if (SUCCEEDED(hr)) {
            LONGLONG time = 0, duration = 0;
            if (SUCCEEDED(pending_->GetSampleTime(&time))) out_sample->SetSampleTime(time);
            if (SUCCEEDED(pending_->GetSampleDuration(&duration))) out_sample->SetSampleDuration(duration);
            outputs->pSample = out_sample;
            out_sample = nullptr;
            pending_->Release(); pending_ = nullptr;
        }
        if (out_sample) out_sample->Release();
        if (out_buffer) out_buffer->Release();
        return hr;
    }

private:
    HRESULT ValidateInputType(IMFMediaType *type)
    {
        if (!type) return E_POINTER;
        GUID major = {}, subtype = {};
        if (FAILED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) ||
            FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
            major != MFMediaType_Video || subtype != MFVideoFormat_IV50) {
            return MF_E_INVALIDMEDIATYPE;
        }
        UINT32 width = 0, height = 0;
        if (FAILED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height)) ||
            width == 0 || height == 0 || width > IV50_MAX_DIMENSION || height > IV50_MAX_DIMENSION) {
            return MF_E_INVALIDMEDIATYPE;
        }
        return S_OK;
    }

    HRESULT BeginDecoder()
    {
        BITMAPINFOHEADER output = {};
        auto *input = reinterpret_cast<BITMAPINFOHEADER *>(input_format_.data());
        output.biSize = sizeof(output); output.biWidth = width_; output.biHeight = -height_;
        output.biPlanes = 1; output.biBitCount = 32; output.biCompression = BI_RGB;
        output.biSizeImage = output_size_;
        int result = iv50_decoder_begin(decoder_, input, &output);
        begun_ = result == ICERR_OK;
        return begun_ ? S_OK : MF_E_NOTACCEPTING;
    }

    void ResetDecoder()
    {
        if (decoder_) iv50_decoder_end(decoder_);
        begun_ = FALSE;
    }

    long ref_count_;
    Iv50Decoder *decoder_;
    IMFMediaType *input_type_;
    IMFMediaType *output_type_;
    IMFSample *pending_;
    LONG width_, height_;
    DWORD output_size_;
    std::vector<BYTE> input_format_;
    BOOL begun_;
};

class Iv50ClassFactory final : public IClassFactory {
public:
    Iv50ClassFactory() : ref_count_(1) { InterlockedIncrement(&g_object_count); }
    ~Iv50ClassFactory() { InterlockedDecrement(&g_object_count); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override
    {
        if (!object) return E_POINTER; *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IClassFactory) {
            *object = static_cast<IClassFactory *>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_count_); }
    ULONG STDMETHODCALLTYPE Release() override { long n=InterlockedDecrement(&ref_count_); if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID iid, void **object) override
    {
        if (outer) return CLASS_E_NOAGGREGATION;
        Iv50Mft *mft = new (std::nothrow) Iv50Mft();
        if (!mft) return E_OUTOFMEMORY;
        HRESULT hr = mft->QueryInterface(iid, object); mft->Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override
    { if (lock) InterlockedIncrement(&g_lock_count); else InterlockedDecrement(&g_lock_count); return S_OK; }
private: long ref_count_;
};

extern "C" HRESULT STDAPICALLTYPE DllCanUnloadNow(void)
{ return g_object_count == 0 && g_lock_count == 0 ? S_OK : S_FALSE; }

extern "C" HRESULT STDAPICALLTYPE DllGetClassObject(REFCLSID clsid, REFIID iid, void **object)
{
    if (clsid != CLSID_IV50_FFMPEG_MFT) return CLASS_E_CLASSNOTAVAILABLE;
    Iv50ClassFactory *factory = new (std::nothrow) Iv50ClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(iid, object); factory->Release(); return hr;
}

static HRESULT RegisterComClass()
{
    WCHAR clsid_text[64]; WCHAR module_path[MAX_PATH];
    if (!StringFromGUID2(CLSID_IV50_FFMPEG_MFT, clsid_text, ARRAYSIZE(clsid_text)) ||
        !GetModuleFileNameW(g_module, module_path, ARRAYSIZE(module_path))) return HRESULT_FROM_WIN32(GetLastError());
    WCHAR key_path[128]; wsprintfW(key_path, L"Software\\Classes\\CLSID\\%s\\InprocServer32", clsid_text);
    HKEY key = nullptr;
    LONG error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, key_path, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE *>(module_path),
        (lstrlenW(module_path) + 1) * sizeof(WCHAR));
    const WCHAR threading[] = L"Both";
    RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ, reinterpret_cast<const BYTE *>(threading), sizeof(threading));
    RegCloseKey(key);
    MFT_REGISTER_TYPE_INFO input = { MFMediaType_Video, MFVideoFormat_IV50 };
    MFT_REGISTER_TYPE_INFO output = { MFMediaType_Video, MFVideoFormat_RGB32 };
    return MFTRegister(CLSID_IV50_FFMPEG_MFT, MFT_CATEGORY_VIDEO_DECODER,
        const_cast<LPWSTR>(L"FFmpeg Indeo 5 Media Foundation Decoder"), 0, 1, &input, 1, &output, nullptr);
}

extern "C" HRESULT STDAPICALLTYPE DllRegisterServer(void)
{ return RegisterComClass(); }

extern "C" HRESULT STDAPICALLTYPE DllUnregisterServer(void)
{
    MFTUnregister(CLSID_IV50_FFMPEG_MFT);
    WCHAR clsid_text[64], key_path[128];
    if (!StringFromGUID2(CLSID_IV50_FFMPEG_MFT, clsid_text, ARRAYSIZE(clsid_text))) return E_FAIL;
    wsprintfW(key_path, L"Software\\Classes\\CLSID\\%s", clsid_text);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, key_path);
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) { g_module = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}
