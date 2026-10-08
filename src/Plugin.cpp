#include "Core.h"
#include "../sdk/PluginInterface.h"
#include <array>

using flclash::Runtime;

class SpeedItem final : public IPluginItem {
public:
    SpeedItem(Runtime& runtime, int direction) : runtime_(runtime), direction_(direction) {}
    const wchar_t* GetItemName() const override { return direction_ == 0 ? L"FlClash 上传" : L"FlClash 下载"; }
    const wchar_t* GetItemId() const override { return direction_ == 0 ? L"FlClashSpeedUpload" : L"FlClashSpeedDownload"; }
    const wchar_t* GetItemLableText() const override { return direction_ == 0 ? L"VPN↑ " : L"VPN↓ "; }
    const wchar_t* GetItemValueSampleText() const override { return L"9999.99 MB/s"; }
    const wchar_t* GetItemValueText() const override {
        thread_local std::array<std::wstring, 2> values;
        auto sample = runtime_.Latest();
        values[direction_] = sample.connected ? flclash::FormatSpeed(direction_ == 0 ? sample.up : sample.down) : L"—";
        return values[direction_].c_str();
    }
    int OnMouseEvent(MouseEventType type, int, int, void* window, int) override {
        if (type != MT_DBCLICKED) return 0;
        flclash::ShowSettingsDialog(runtime_, static_cast<HWND>(window));
        return 1;
    }
private:
    Runtime& runtime_;
    int direction_;
};

class FlClashPlugin final : public ITMPlugin {
public:
    FlClashPlugin() : upload_(runtime_, 0), download_(runtime_, 1) {}
    IPluginItem* GetItem(int index) override {
        if (index == 0) return &upload_;
        if (index == 1) return &download_;
        return nullptr;
    }
    void DataRequired() override { runtime_.Start(); }
    OptionReturn ShowOptionsDialog(void* parent) override {
        return flclash::ShowSettingsDialog(runtime_, static_cast<HWND>(parent)) ? OR_OPTION_CHANGED : OR_OPTION_UNCHANGED;
    }
    const wchar_t* GetInfo(PluginInfoIndex index) override {
        switch (index) {
        case TMI_NAME: return L"FlClashSpeed";
        case TMI_DESCRIPTION: return L"FlClash 实时上传/下载速度，自动发现本机控制器并重连。";
        case TMI_AUTHOR: return L"LeoVeeeee / FlClashSpeed contributors";
        case TMI_COPYRIGHT: return L"FlClashSpeed 2026";
        case TMI_VERSION: return L"2.0.1";
        case TMI_URL: return L"https://github.com/LeoVeeeee/FlClashSpeed";
        default: return L"";
        }
    }
    const wchar_t* GetTooltipInfo() override {
        thread_local std::wstring text;
        auto sample = runtime_.Latest();
        text = L"FlClashSpeed: " + sample.status;
        if (sample.connected) text += L"\n↑ " + flclash::FormatSpeed(sample.up) + L"  ↓ " + flclash::FormatSpeed(sample.down);
        text += L"\n统计内核处理的流量，包含 DIRECT 直连。";
        return text.c_str();
    }
    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override {
        if (index == EI_CONFIG_DIR) runtime_.ConfigureFolder(data);
    }
    void* GetPluginIcon() override { return LoadIconW(nullptr, IDI_INFORMATION); }
    int GetCommandCount() override { return 2; }
    const wchar_t* GetCommandName(int index) override {
        return index == 0 ? L"重新连接 FlClash（点击执行）" : index == 1 ? L"FlClash 连接选项…" : L"";
    }
    void OnPluginCommand(int index, void* parent, void*) override {
        if (index == 0) runtime_.Reconnect();
        if (index == 1) ShowOptionsDialog(parent);
    }
    void Stop() { runtime_.Stop(); }
private:
    Runtime runtime_;
    SpeedItem upload_, download_;
};

static FlClashPlugin& Instance() {
    // Process-lifetime singleton; no destructor waits under the Windows loader lock.
    static auto* instance = new FlClashPlugin;
    return *instance;
}
extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance() { return &Instance(); }
// For diagnostic hosts: stop all callbacks before ending tests. Not used by TrafficMonitor.
extern "C" __declspec(dllexport) void FlClashSpeedShutdown() { Instance().Stop(); }
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
