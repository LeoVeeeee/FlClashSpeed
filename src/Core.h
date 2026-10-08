#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>

namespace flclash {
struct Settings {
    std::wstring configPath;
    std::wstring endpoint;
    std::wstring secret;
};
struct Controller {
    std::wstring host{L"127.0.0.1"};
    INTERNET_PORT port{9090};
    bool secure{};
    std::wstring secret;
};
struct Snapshot {
    uint64_t up{}, down{};
    bool connected{};
    ULONGLONG received{};
    std::wstring status{L"正在连接 FlClash…"};
};
std::wstring Widen(const std::string& value);
std::string Narrow(const std::wstring& value);
std::string ReadUtf8File(const std::wstring& path, size_t maxBytes = 8 * 1024 * 1024);
std::wstring FormatSpeed(uint64_t bytes);
Controller ParseEndpoint(const std::wstring& value, const std::wstring& secret);
Controller Discover(const Settings& settings);
Snapshot ParseTraffic(const std::string& line);
Settings LoadSettings(const std::wstring& path);
void SaveSettings(const std::wstring& path, const Settings& settings);

class Runtime {
public:
    Runtime();
    void Start();
    void Stop();
    void Reconnect();
    void ConfigureFolder(const wchar_t* folder);
    Settings GetSettings();
    void UpdateSettings(const Settings& settings);
    Snapshot Latest();
private:
    static DWORD WINAPI Worker(void* context);
    void Run();
    void Stream(const Controller& controller, uint64_t generation);
    void Publish(Snapshot sample);
    void PublishIfCurrent(Snapshot sample, uint64_t generation);
    SRWLOCK lock_ = SRWLOCK_INIT;
    Settings settings_;
    std::wstring settingsFile_;
    std::wstring settingsError_;
    Snapshot latest_;
    HANDLE stop_{}, wake_{}, thread_{};
    std::atomic<bool> started_{false};
    std::atomic<uint64_t> generation_{};
};
bool ShowSettingsDialog(Runtime& runtime, HWND parent);
}
