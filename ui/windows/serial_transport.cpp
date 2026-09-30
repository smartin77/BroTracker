#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include "teensy_identity.h"
#include "diagnostics.h"
#include "ui/serial_transport.h"
#include <atomic>
#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace {
// Device setup class Ports. Only present CDC nodes with the expected USB ID
// are considered; unrelated COM ports are never opened or sent probes.
constexpr GUID ports_class{0x4d36e978,0xe325,0x11ce,{0xbf,0xc1,0x08,0x00,0x2b,0xe1,0x03,0x18}};
class WindowsSerialTransport final : public SerialTransport {
    enum State { Idle, Discovering, Opened, Failed };
    std::atomic<State> state_{Idle};
    std::atomic<bool> stop_{false};
    std::thread worker_;
    std::mutex mutex_;
    std::array<char, 4096> rx_{};
    unsigned head_ = 0, size_ = 0;
    std::array<char, 32> tx_{};
    unsigned tx_size_ = 0;
    int tx_result_ = 0;
    char error_[256]{};
    std::string last_discovery_;
    FILE* log_ = nullptr;
    void Log(const char* message) {
        WindowsDiagnostic(log_, "Windows CDC", message);
    }
    void Fail(const char* operation, DWORD code = GetLastError()) {
        char description[160]{};
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, code, 0, description, sizeof(description), nullptr);
        std::snprintf(error_, sizeof(error_), "%s: Win32 %lu %s", operation,
                      static_cast<unsigned long>(code), description);
        Log(error_);
    }
    std::wstring Discover() {
        HDEVINFO devices = SetupDiGetClassDevsW(&ports_class, nullptr, nullptr, DIGCF_PRESENT);
        if (devices == INVALID_HANDLE_VALUE) { Fail("enumeration failed"); return {}; }
        std::vector<std::wstring> matches;
        for (DWORD i = 0; !stop_; ++i) {
            SP_DEVINFO_DATA info{}; info.cbSize = sizeof(info);
            if (!SetupDiEnumDeviceInfo(devices, i, &info)) break;
            wchar_t identity[512]{};
            if (!SetupDiGetDeviceInstanceIdW(devices, &info, identity, 512, nullptr) ||
                !IsTeensyIdentity(identity)) continue;
            HKEY key = SetupDiOpenDevRegKey(devices, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
            if (key == INVALID_HANDLE_VALUE) continue;
            wchar_t port[64]{}; DWORD bytes = sizeof(port), type = 0;
            LONG result = RegQueryValueExW(key, L"PortName", nullptr, &type,
                                           reinterpret_cast<BYTE*>(port), &bytes);
            RegCloseKey(key);
            if (result != ERROR_SUCCESS || type != REG_SZ || bytes >= sizeof(port)) continue;
            std::wstring name(port);
            if (name.size() <= 3 || name.substr(0, 3) != L"COM" ||
                name.find_first_not_of(L"0123456789", 3) != std::wstring::npos) continue;
            char line[768];
            std::snprintf(line, sizeof(line), "discovered %ls -> %ls", identity, port);
            Log(line);
            matches.push_back(name);
        }
        SetupDiDestroyDeviceInfoList(devices);
        if (matches.size() != 1) {
            const std::string reason = matches.empty() ? "waiting: no Teensy CDC device present" :
                "ambiguous: multiple Teensy CDC devices; refusing to choose";
            if (reason != last_discovery_) { Log(reason.c_str()); last_discovery_ = reason; }
            return {};
        }
        last_discovery_.clear();
        return L"\\\\.\\" + matches.front();
    }
    void Run() {
        Log("discovery begin");
        const std::wstring device = Discover();
        Log("discovery end");
        if (device.empty() || stop_) { state_ = Failed; return; }
        Log("COM open begin");
        HANDLE port = CreateFileW(device.c_str(), GENERIC_READ | GENERIC_WRITE,
                                  0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (port == INVALID_HANDLE_VALUE) { Fail("open failed (port may be busy or removed)"); state_ = Failed; return; }
        Log("COM open complete; configuration begin");
        DCB config{}; config.DCBlength = sizeof(config);
        bool configured = GetCommState(port, &config);
        config.BaudRate = CBR_115200; config.ByteSize = 8; config.Parity = NOPARITY;
        config.StopBits = ONESTOPBIT; config.fBinary = TRUE; config.fParity = FALSE;
        config.fOutxCtsFlow = config.fOutxDsrFlow = FALSE;
        config.fDtrControl = DTR_CONTROL_ENABLE; config.fRtsControl = RTS_CONTROL_ENABLE;
        config.fDsrSensitivity = config.fOutX = config.fInX = config.fAbortOnError = FALSE;
        // All OS serial calls occur on this worker, never the SDL thread.
        // Reads return immediately; writes have a finite driver timeout.
        COMMTIMEOUTS timeouts{}; timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.WriteTotalTimeoutConstant = 100;
        configured = configured && SetCommState(port, &config) &&
            SetCommTimeouts(port, &timeouts) && PurgeComm(port, PURGE_RXCLEAR | PURGE_TXCLEAR);
        if (!configured) { Fail("CDC configuration failed"); CloseHandle(port); state_ = Failed; return; }
        Log("opened identified Teensy; 115200 8N1, DTR/RTS enabled");
        state_ = Opened;
        bool first_state = true;
        char received_line[256]{}; unsigned received_used = 0;
        while (!stop_) {
            char outgoing[32]{}; unsigned outgoing_size = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (tx_size_ && !tx_result_) {
                    outgoing_size = tx_size_;
                    std::memcpy(outgoing, tx_.data(), outgoing_size);
                }
            }
            if (outgoing_size) {
                if (std::strncmp(outgoing, "BTTEST1 HELLO", 12) == 0) Log("HELLO WriteFile begin");
                DWORD written = 0;
                if (!WriteFile(port, outgoing, outgoing_size, &written, nullptr) || written == 0) {
                    Fail("write failed/timeout"); break;
                }
                if (std::strncmp(outgoing, "BTTEST1 HELLO", 12) == 0) Log("HELLO WriteFile complete");
                std::lock_guard<std::mutex> lock(mutex_);
                tx_result_ = static_cast<int>(written);
            }
            char incoming[512]; DWORD count = 0;
            if (!ReadFile(port, incoming, sizeof(incoming), &count, nullptr)) { Fail("read failed/disconnected"); break; }
            // Timestamp only startup landmarks and the first protocol state.
            // The shared parser still owns handshake/command semantics.
            for (DWORD i = 0; i < count; ++i) {
                const char c = incoming[i];
                if (c == '\r') continue;
                if (c == '\n') {
                    received_line[received_used] = '\0';
                    if (first_state && std::strncmp(received_line, "BTTEST1 STATE ", 14) == 0) {
                        Log(received_line); first_state = false;
                    } else if (std::strcmp(received_line, "USB AUDIO: ENABLED") == 0 ||
                               std::strstr(received_line, "playback armed Samples/test.wav")) Log(received_line);
                    received_used = 0;
                } else if (received_used + 1 < sizeof(received_line)) received_line[received_used++] = c;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (count > rx_.size() - size_) {
                    Fail("bounded receive buffer overflow", ERROR_BUFFER_OVERFLOW); break;
                }
                for (DWORD i = 0; i < count; ++i) rx_[(head_ + size_++) % rx_.size()] = incoming[i];
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CloseHandle(port);
        Log("serial handle closed");
        state_ = Failed;
    }
public:
    ~WindowsSerialTransport() override { Close(); if (worker_.joinable()) worker_.join(); }
    bool Open(FILE* log, const char*) override {
        if (state_ == Opened && !stop_) return true;
        if (state_ == Discovering || (stop_ && state_ == Opened)) return false;
        if (worker_.joinable()) worker_.join(); // worker has already finished
        log_ = log; stop_ = false; head_ = size_ = tx_size_ = 0; tx_result_ = 0;
        state_ = Discovering;
        worker_ = std::thread([this] { Run(); });
        return false;
    }
    void Close() override { stop_ = true; } // No UI-thread wait for serial I/O.
    bool Healthy() override { return state_ == Opened && !stop_; }
    int Read(char* data, unsigned capacity) override {
        if (!Healthy()) return -1;
        std::lock_guard<std::mutex> lock(mutex_);
        unsigned count = 0;
        while (count < capacity && size_) { data[count++] = rx_[head_]; head_ = (head_ + 1) % rx_.size(); --size_; }
        return static_cast<int>(count);
    }
    int Write(const char* data, unsigned count) override {
        if (!Healthy()) return -1;
        std::lock_guard<std::mutex> lock(mutex_);
        if (tx_result_) { int result = tx_result_; tx_result_ = 0; tx_size_ = 0; return result; }
        if (!tx_size_) {
            if (count > tx_.size()) return -1;
            std::memcpy(tx_.data(), data, count); tx_size_ = count;
        }
        return 0;
    }
    const char* Error() const override { return error_; }
};
}
std::unique_ptr<SerialTransport> MakeSerialTransport() { return std::make_unique<WindowsSerialTransport>(); }
