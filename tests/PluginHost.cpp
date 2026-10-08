#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../sdk/PluginInterface.h"
#include "../src/Core.h"
#include "../third_party/json.hpp"
#include <iostream>
#include <stdexcept>
#include <chrono>

using Json = nlohmann::json;
int wmain(int argc, wchar_t** argv) {
    if (argc < 4) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    HMODULE module = LoadLibraryW(argv[1]);
    if (!module) { std::cout << "LoadLibrary error " << GetLastError() << std::endl; return 3; }
    auto instance = reinterpret_cast<ITMPlugin*(*)()>(GetProcAddress(module, "TMPluginGetInstance"));
    auto shutdown = reinterpret_cast<void(*)()>(GetProcAddress(module, "FlClashSpeedShutdown"));
    try {
        for (const auto& test : std::initializer_list<std::pair<uint64_t, const wchar_t*>>{
            {0, L"0.00 KB/s"}, {1024, L"1.00 KB/s"}, {10235, L"10.0 KB/s"}, {10240, L"10.0 KB/s"},
            {102359, L"100 KB/s"},
            {126412, L"123.45 KB/s"}, {102400, L"100 KB/s"}, {1048576, L"1.00 MB/s"},
            {1572864, L"1.50 MB/s"}, {10485760, L"10.0 MB/s"}, {104857600, L"100 MB/s"}})
            if (flclash::FormatSpeed(test.first) != test.second) throw std::runtime_error("Speed format boundary failed");
        if (!instance || !shutdown) throw std::runtime_error("Missing exports");
        ITMPlugin* plugin = instance();
        if (!plugin || plugin != instance() || plugin->GetAPIVersion() != 7 || plugin->GetItem(-1) || plugin->GetItem(2))
            throw std::runtime_error("Invalid plugin interface");
        auto* up = plugin->GetItem(0);
        auto* down = plugin->GetItem(1);
        if (!up || !down || up->IsCustomDraw() || down->IsCustomDraw() ||
            std::wstring(up->GetItemId()) == down->GetItemId() || plugin->GetCommandCount() != 2)
            throw std::runtime_error("Invalid display items");
        for (int i = 0; i < ITMPlugin::TMI_MAX; ++i)
            if (!plugin->GetInfo(static_cast<ITMPlugin::PluginInfoIndex>(i))) throw std::runtime_error("Null metadata");
        plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, argv[2]);
        std::wstring roundTripPath = std::wstring(argv[2]) + L"\\roundtrip.ini";
        flclash::Settings expected{L"C:\\测试目录\\config.yaml", L"http://127.0.0.1:9090", L"test-secret-测试"};
        flclash::SaveSettings(roundTripPath, expected);
        auto restored = flclash::LoadSettings(roundTripPath);
        if (restored.configPath != expected.configPath || restored.endpoint != expected.endpoint || restored.secret != expected.secret)
            throw std::runtime_error("UTF-16/DPAPI round trip failed");
        auto saved = flclash::ReadUtf8File(roundTripPath);
        std::string plainBytes(reinterpret_cast<const char*>(expected.secret.data()), expected.secret.size() * sizeof(wchar_t));
        if (saved.find(plainBytes) != std::string::npos) throw std::runtime_error("Plaintext secret was persisted");
        auto invalid = expected;
        invalid.configPath = L"bad\nendpoint=evil";
        bool rejected = false;
        try { flclash::SaveSettings(roundTripPath, invalid); } catch (const std::exception&) { rejected = true; }
        if (!rejected || flclash::LoadSettings(roundTripPath).secret != expected.secret)
            throw std::runtime_error("Invalid write did not preserve previous settings");
        expected.secret = L"replacement-secret";
        flclash::SaveSettings(roundTripPath, expected);
        if (flclash::LoadSettings(roundTripPath).secret != expected.secret)
            throw std::runtime_error("Atomic replacement did not reload");
        int seconds = _wtoi(argv[3]);
        if (argc > 4 && std::wstring(argv[4]) == L"--options") {
            plugin->DataRequired();
            plugin->ShowOptionsDialog(nullptr);
        }
        auto deadline = GetTickCount64() + seconds * 1000;
        auto reconnectAt = GetTickCount64() + 2000;
        bool reconnect = argc > 4 && std::wstring(argv[4]) == L"--reconnect";
        while (GetTickCount64() < deadline) {
            auto start = std::chrono::steady_clock::now();
            plugin->DataRequired();
            if (reconnect && GetTickCount64() >= reconnectAt) {
                for (int i = 0; i < 20; ++i) plugin->OnPluginCommand(0, nullptr, nullptr);
                reconnect = false;
            }
            auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (delay > 100) throw std::runtime_error("DataRequired blocks the host");
            const auto* upPointer = up->GetItemValueText();
            std::wstring upText(upPointer), downText(down->GetItemValueText());
            if (upText != upPointer) throw std::runtime_error("Item strings share an unstable buffer");
            Json record{{"up", flclash::Narrow(upText)}, {"down", flclash::Narrow(downText)},
                        {"status", flclash::Narrow(plugin->GetTooltipInfo())}, {"call_ms", delay}};
            std::cout << record.dump() << std::endl;
            Sleep(200);
        }
        shutdown();
        FreeLibrary(module);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        if (shutdown) shutdown();
        FreeLibrary(module);
        return 1;
    }
}
