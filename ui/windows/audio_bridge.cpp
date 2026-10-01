#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <setupapi.h>
#include <devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include "audio_bridge.h"
#include "audio_buffer.h"
#include "audio_discovery.h"
#include "diagnostics.h"
#include "teensy_identity.h"

namespace {
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    Com() = default;
    Com(const Com&) = delete;
    T* operator->() const { return p; }
    T** Out() { if (p) p->Release(); p = nullptr; return &p; }
};
struct Event {
    HANDLE h;
    explicit Event(bool manual = true) : h(CreateEventW(nullptr, manual, FALSE, nullptr)) {}
    ~Event() { if (h) CloseHandle(h); }
};
using Failure = WindowsAudioFailure;
void Check(HRESULT hr, const char* what) { if (FAILED(hr)) throw Failure(what, hr); }
std::string Utf8(const std::wstring& value) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size ? size : 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), int(result.size()), nullptr, nullptr);
    result.pop_back(); return result;
}
std::wstring Id(IMMDevice* device) {
    LPWSTR value = nullptr; Check(device->GetId(&value), "endpoint ID");
    std::wstring result(value); CoTaskMemFree(value); return result;
}
std::wstring Name(IMMDevice* device) {
    Com<IPropertyStore> props; Check(device->OpenPropertyStore(STGM_READ, props.Out()), "endpoint properties");
    PROPVARIANT value{};
    Check(props->GetValue(PKEY_Device_FriendlyName, &value), "endpoint name");
    std::wstring result = value.vt == VT_LPWSTR ? value.pwszVal : L"(unnamed)";
    PropVariantClear(&value); return result;
}
GUID Container(IMMDevice* device) {
    Com<IPropertyStore> props; Check(device->OpenPropertyStore(STGM_READ, props.Out()), "endpoint container properties");
    PROPVARIANT value{}; Check(props->GetValue(PKEY_Device_ContainerId, &value), "endpoint container");
    GUID result{};
    if (value.vt == VT_CLSID && value.puuid) result = *value.puuid;
    else if (value.vt == VT_LPWSTR) CLSIDFromString(value.pwszVal, &result);
    PropVariantClear(&value);
    if (IsEqualGUID(result, GUID_NULL)) throw Failure("endpoint has no verifiable container identity", E_INVALIDARG);
    return result;
}
struct UsbDevice { GUID container; std::wstring identity; };
std::vector<UsbDevice> UsbDevices() {
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) throw Failure("USB enumeration", HRESULT_FROM_WIN32(GetLastError()));
    struct Close { HDEVINFO h; ~Close() { SetupDiDestroyDeviceInfoList(h); } } close{set};
    std::vector<UsbDevice> devices;
    for (DWORD n = 0;; ++n) {
        SP_DEVINFO_DATA info{}; info.cbSize = sizeof(info);
        if (!SetupDiEnumDeviceInfo(set, n, &info)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) throw Failure("USB enumeration", HRESULT_FROM_WIN32(GetLastError()));
            break;
        }
        wchar_t identity[512]{};
        if (!SetupDiGetDeviceInstanceIdW(set, &info, identity, 512, nullptr) || !IsTeensyIdentity(identity)) continue;
        GUID container{}; DEVPROPTYPE type = 0;
        if (!SetupDiGetDevicePropertyW(set, &info, &DEVPKEY_Device_ContainerId, &type,
                reinterpret_cast<BYTE*>(&container), sizeof(container), nullptr, 0) ||
                type != DEVPROP_TYPE_GUID || IsEqualGUID(container, GUID_NULL)) continue;
        auto found = std::find_if(devices.begin(), devices.end(), [&](const UsbDevice& d) {
            return IsEqualGUID(d.container, container);
        });
        if (found == devices.end()) devices.push_back({container, identity});
        else if (std::wstring(identity).find(L"&MI_") == std::wstring::npos) found->identity = identity;
    }
    return devices;
}
const UsbDevice* Match(const GUID& container, const std::vector<UsbDevice>& devices) {
    for (const auto& device : devices) if (IsEqualGUID(container, device.container)) return &device;
    return nullptr;
}
// Notifications never discover devices, allocate or log. The worker restarts
// outside its audio loop; callback lifetime ends before the enumerator/log.
class Notification final : public IMMNotificationClient {
    std::atomic<ULONG> refs_{1};
public:
    std::atomic<unsigned> generation{0};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --refs_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eMultimedia) ++generation;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { ++generation; return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { ++generation; return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { ++generation; return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
};
struct Session {
    Event capture_ready{false}, render_ready{false};
    Com<IAudioClient> capture, render;
    Com<IAudioCaptureClient> input;
    Com<IAudioRenderClient> output;
    bool capture_started = false, render_started = false;
    ~Session() {
        if (capture_started) capture->Stop();
        if (render_started) render->Stop();
    }
};
}

struct WindowsAudioBridge::Impl {
    FILE* log;
    Event stop;
    std::thread worker;
    std::atomic<uint64_t> captured{0}, nonzero{0}, rendered{0}, missing{0}, dropped{0}, discontinuities{0}, underruns{0};
    std::atomic<unsigned> queued{0}, peak_ppm{0};
    std::atomic<bool> running{false};
    ULONGLONG last_report = 0;
    explicit Impl(FILE* file) : log(file) {
        if (!stop.h) throw std::runtime_error("Cannot create audio shutdown event");
        worker = std::thread([this] { Run(); });
    }
    bool Stopped() const { return WaitForSingleObject(stop.h, 0) == WAIT_OBJECT_0; }
    void Log(const std::string& text) { WindowsDiagnostic(log, "Windows audio", text.c_str()); }
    void Format(IAudioClient* client, const char* label) {
        WAVEFORMATEX* mix = nullptr; Check(client->GetMixFormat(&mix), "mix format");
        const auto tag = mix->wFormatTag;
        const auto sub = tag == WAVE_FORMAT_EXTENSIBLE ? reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->SubFormat.Data1 : tag;
        char line[256];
        std::snprintf(line, sizeof(line), "%s mix: tag=%u subtype=%lu channels=%u rate=%lu bits=%u align=%u; client=stereo float32 44100 Hz, Windows auto-conversion/SRC",
            label, tag, static_cast<unsigned long>(sub), mix->nChannels,
            static_cast<unsigned long>(mix->nSamplesPerSec), mix->wBitsPerSample, mix->nBlockAlign);
        CoTaskMemFree(mix); Log(line);
    }
    void Initialize(IMMDevice* device, Com<IAudioClient>& client, HANDLE ready, const char* label) {
        Check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.Out())), "activate audio client");
        Format(client.p, label);
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT; format.nChannels = 2;
        format.nSamplesPerSec = 44100; format.wBitsPerSample = 32;
        format.nBlockAlign = 8; format.nAvgBytesPerSec = 44100 * 8;
        Check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | AUDCLNT_STREAMFLAGS_NOPERSIST | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            200000, 0, &format, nullptr), "shared stereo 44100 initialization");
        Check(client->SetEventHandle(ready), "audio buffer event");
        UINT32 size = 0; REFERENCE_TIME period = 0, minimum = 0, latency = 0;
        Check(client->GetBufferSize(&size), "buffer size");
        Check(client->GetDevicePeriod(&period, &minimum), "device period");
        Check(client->GetStreamLatency(&latency), "stream latency setting");
        char line[256];
        std::snprintf(line, sizeof(line), "%s buffer=%u client frames; engine period=%lld minimum=%lld stream latency=%lld (100ns units); configuration, NOT measured end-to-end latency",
            label, size, static_cast<long long>(period), static_cast<long long>(minimum), static_cast<long long>(latency));
        Log(line);
    }
    void Route(IMMDeviceEnumerator* enumerator, Notification* notify) {
        const unsigned generation = notify->generation;
        const auto devices = UsbDevices();
        Com<IMMDeviceCollection> collection;
        Check(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, collection.Out()), "capture enumeration");
        UINT count = 0; Check(collection->GetCount(&count), "capture count");
        Com<IMMDevice> capture, render;
        unsigned matches = 0;
        for (UINT i = 0; i < count; ++i) {
            Com<IMMDevice> candidate; Check(collection->Item(i, candidate.Out()), "capture endpoint");
            const auto container = TryCaptureContainer([&] { return Container(candidate.p); },
                [&](const Failure& error) {
                    char detail[256];
                    std::snprintf(detail, sizeof(detail),
                        "skipping capture candidate index=%u: %s (HRESULT=0x%08lx)",
                        i, error.what(), static_cast<unsigned long>(error.code));
                    Log(detail);
                });
            if (!container) continue;
            const auto* usb = Match(*container, devices);
            if (!usb) continue;
            wchar_t guid[40]{}; StringFromGUID2(usb->container, guid, 40);
            Log("capture candidate: " + Utf8(Name(candidate.p)) + " endpoint=" + Utf8(Id(candidate.p)) +
                " USB=" + Utf8(usb->identity) + " container=" + Utf8(guid));
            ++matches;
            candidate->AddRef(); *capture.Out() = candidate.p;
        }
        if (matches != 1) throw Failure(matches ? "ambiguous USB capture endpoints; refusing to choose" :
            "waiting: no active capture endpoint for USB 16c0:048a", HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
        Check(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, render.Out()), "default multimedia output unavailable");
        if (Match(Container(render.p), devices)) throw Failure("default output is BroTracker USB; refusing audio feedback route", E_INVALIDARG);
        Log("default multimedia render: " + Utf8(Name(render.p)) + " endpoint=" + Utf8(Id(render.p)));
        Session session;
        if (!session.capture_ready.h || !session.render_ready.h)
            throw Failure("audio buffer events", HRESULT_FROM_WIN32(GetLastError()));
        Initialize(capture.p, session.capture, session.capture_ready.h, "capture");
        Initialize(render.p, session.render, session.render_ready.h, "render");
        Check(session.capture->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(session.input.Out())), "capture service");
        Check(session.render->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(session.output.Out())), "render service");
        UINT32 capacity = 0; Check(session.render->GetBufferSize(&capacity), "render capacity");
        BYTE* output = nullptr;
        Check(session.output->GetBuffer(capacity, &output), "prime render");
        Check(session.output->ReleaseBuffer(capacity, AUDCLNT_BUFFERFLAGS_SILENT), "prime silence");
        WindowsAudioBuffer buffer;
        Check(session.capture->Start(), "capture start"); session.capture_started = true;
        Check(session.render->Start(), "render start"); session.render_started = true;
        Log("routing started; bounded queue=8192 frames, target=1323; drift correction <=3000ppm; WASAPI buffer events; no firmware commands");
        running = true;
        uint64_t local_capture = 0, local_nonzero = 0, local_disc = 0, local_underruns = 0;
        unsigned local_peak = 0;
        const auto c0 = captured.load(), n0 = nonzero.load(), r0 = rendered.load(), m0 = missing.load(),
            d0 = dropped.load(), x0 = discontinuities.load(), u0 = underruns.load();
        ULONGLONG capture_activity = GetTickCount64(), render_activity = capture_activity;
        HRESULT failure = S_OK; const char* operation = "audio session ended";
        HANDLE events[] = {stop.h, session.capture_ready.h, session.render_ready.h};
        // No string construction, allocation, discovery, or file logging below.
        // A packet-count cap also bounds work if the capture driver floods us.
        while (!Stopped() && notify->generation == generation) {
            UINT32 packet = 0;
            failure = session.input->GetNextPacketSize(&packet); operation = "capture packet/disconnected";
            if (FAILED(failure)) break;
            for (unsigned packets = 0; packet && packets < 32; ++packets) {
                BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                failure = session.input->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(failure)) break;
                if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) { ++local_disc; buffer.Reset(); }
                const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                const auto* samples = reinterpret_cast<const float*>(data);
                if (!silent) for (UINT32 f = 0; f < frames; ++f) {
                    const float level = std::max(std::abs(samples[f*2]), std::abs(samples[f*2+1]));
                    if (level > .000001f) ++local_nonzero;
                    if (std::isfinite(level)) local_peak = std::max(local_peak, unsigned(std::min(level, 1.f) * 1000000));
                }
                buffer.Push(samples, frames, silent); local_capture += frames;
                failure = session.input->ReleaseBuffer(frames);
                if (FAILED(failure)) break;
                capture_activity = GetTickCount64();
                failure = session.input->GetNextPacketSize(&packet);
                if (FAILED(failure)) break;
            }
            if (FAILED(failure)) break;
            UINT32 padding = 0; operation = "render padding/disconnected";
            failure = session.render->GetCurrentPadding(&padding);
            if (FAILED(failure)) break;
            if (padding > capacity) { failure = E_UNEXPECTED; break; }
            if (!padding) ++local_underruns;
            const UINT32 available = capacity - padding;
            if (available) {
                operation = "render buffer/disconnected";
                failure = session.output->GetBuffer(available, &output);
                if (FAILED(failure)) break;
                buffer.Render(reinterpret_cast<float*>(output), available);
                failure = session.output->ReleaseBuffer(available, 0);
                if (FAILED(failure)) break;
                render_activity = GetTickCount64();
            }
            captured = c0 + local_capture; nonzero = n0 + local_nonzero;
            rendered = r0 + buffer.rendered; missing = m0 + buffer.missing; dropped = d0 + buffer.dropped;
            discontinuities = x0 + local_disc; underruns = u0 + local_underruns;
            queued = buffer.Size(); peak_ppm = local_peak;
            if (GetTickCount64() - capture_activity > 3000 || GetTickCount64() - render_activity > 3000) {
                failure = HRESULT_FROM_WIN32(ERROR_TIMEOUT); operation = "audio stream stalled"; break;
            }
            const DWORD wait = WaitForMultipleObjects(3, events, FALSE, 500);
            if (wait == WAIT_FAILED) { failure = HRESULT_FROM_WIN32(GetLastError()); operation = "audio event wait"; break; }
        }
        running = false;
        // Stop and release both clients on scope exit before retry/backoff.
        if (FAILED(failure)) throw Failure(operation, failure);
        Log(Stopped() ? "routing stopped on terminal shutdown" : "device/default-output change; releasing endpoints and rediscovering");
    }
    void Run() noexcept {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(com)) { Log("COM initialization failed; audio disabled"); return; }
        try {
            DWORD task_index = 0;
            HANDLE task = AvSetMmThreadCharacteristicsW(L"Audio", &task_index);
            struct Scheduling { HANDLE h; ~Scheduling() { if (h) AvRevertMmThreadCharacteristics(h); } } scheduling{task};
            Log(task ? "worker scheduling: MMCSS Audio" : "MMCSS unavailable; using normal worker priority");
            Com<IMMDeviceEnumerator> enumerator;
            Check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.Out())), "audio enumerator");
            Com<Notification> notify; notify.p = new Notification;
            Check(enumerator->RegisterEndpointNotificationCallback(notify.p), "audio notifications");
            struct Unregister { IMMDeviceEnumerator* e; Notification* n; ~Unregister() { e->UnregisterEndpointNotificationCallback(n); } } unregister{enumerator.p, notify.p};
            unsigned delay = 1000;
            while (!Stopped()) {
                const auto began = GetTickCount64();
                try { Route(enumerator.p, notify.p); delay = 1000; }
                catch (const Failure& error) {
                    running = false;
                    if (GetTickCount64() - began > 10000) delay = 1000;
                    char detail[512];
                    std::snprintf(detail, sizeof(detail), "%s: HRESULT=0x%08lx; endpoints released; retry in %ums",
                        error.what(), static_cast<unsigned long>(error.code), delay);
                    Log(detail);
                }
                if (!Stopped()) WaitForSingleObject(stop.h, delay);
                delay = std::min(delay * 2, 8000u);
            }
        } catch (const std::exception& error) { Log(std::string("audio worker disabled: ") + error.what()); }
        CoUninitialize(); Log("worker stopped; all audio endpoints released");
    }
    void Report(const char* label) {
        char line[512];
        std::snprintf(line, sizeof(line), "%s running=%u captured=%llu nonzero=%llu rendered=%llu silence-fill=%llu dropped=%llu discontinuities=%llu render-empty=%llu queued=%u peak=%.6f",
            label, unsigned(running.load()), (unsigned long long)captured.load(), (unsigned long long)nonzero.load(),
            (unsigned long long)rendered.load(), (unsigned long long)missing.load(), (unsigned long long)dropped.load(),
            (unsigned long long)discontinuities.load(), (unsigned long long)underruns.load(), queued.load(), peak_ppm.load()/1000000.);
        Log(line);
    }
    void Stop() {
        SetEvent(stop.h);
        if (worker.joinable()) { worker.join(); Report("shutdown summary"); }
    }
    ~Impl() { Stop(); }
};
WindowsAudioBridge::WindowsAudioBridge(FILE* log) : impl_(std::make_unique<Impl>(log)) {}
WindowsAudioBridge::~WindowsAudioBridge() = default;
void WindowsAudioBridge::Stop() { impl_->Stop(); }
void WindowsAudioBridge::PollDiagnostics() {
    if (GetTickCount64() - impl_->last_report >= 5000) {
        impl_->last_report = GetTickCount64(); impl_->Report("flow");
    }
}
