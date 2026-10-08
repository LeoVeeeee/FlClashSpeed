#include "Core.h"
#include <commdlg.h>
#include <stdexcept>

namespace flclash {
namespace {
enum { Config = 100, Browse, Endpoint, Secret, Status, Reconnect, Help };
struct DialogState { Runtime* runtime; bool changed{}; };
HWND Control(HWND parent, const wchar_t* type, const wchar_t* text, int x, int y, int width, int height, int id = 0, DWORD style = 0) {
    const auto dpi = GetDpiForWindow(parent);
    auto scale = [dpi](int value) { return MulDiv(value, dpi ? dpi : 96, 96); };
    HWND window = CreateWindowExW(type == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0,
        type, text, WS_CHILD | WS_VISIBLE | style, scale(x), scale(y), scale(width), scale(height), parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(window, WM_SETFONT, SendMessageW(parent, WM_GETFONT, 0, 0), TRUE);
    return window;
}
std::wstring Text(HWND dialog, int id) {
    HWND control = GetDlgItem(dialog, id);
    int length = GetWindowTextLengthW(control);
    std::wstring result(length + 1, L'\0');
    GetWindowTextW(control, result.data(), length + 1);
    result.resize(length);
    return result;
}
INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<DialogState*>(lp);
        SetWindowLongPtrW(dialog, DWLP_USER, lp);
        auto settings = state->runtime->GetSettings();
        RECT rect{}; GetClientRect(dialog, &rect);
        const auto dpi = GetDpiForWindow(dialog);
        int width = MulDiv(rect.right, 96, dpi ? dpi : 96);
        Control(dialog, L"STATIC", L"默认自动读取 FlClash 配置，无需填写。\r\n请在 FlClash → 配置 → 常规中开启“外部控制器”。", 18, 12, width - 36, 40);
        Control(dialog, L"STATIC", L"FlClash 配置文件（可选）", 18, 63, width - 36, 20);
        Control(dialog, L"EDIT", settings.configPath.c_str(), 18, 87, width - 116, 26, Config, WS_TABSTOP | ES_AUTOHSCROLL);
        Control(dialog, L"BUTTON", L"浏览…", width - 88, 86, 70, 28, Browse, WS_TABSTOP);
        Control(dialog, L"STATIC", L"手动控制器地址（留空为自动；不要填写代理端口 7890）", 18, 124, width - 36, 20);
        Control(dialog, L"EDIT", settings.endpoint.c_str(), 18, 148, width - 36, 26, Endpoint, WS_TABSTOP | ES_AUTOHSCROLL);
        Control(dialog, L"STATIC", L"手动密钥（仅用于手动地址）", 18, 185, width - 36, 20);
        Control(dialog, L"EDIT", settings.secret.c_str(), 18, 209, width - 36, 26, Secret, WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD);
        Control(dialog, L"STATIC", state->runtime->Latest().status.c_str(), 18, 252, width - 36, 40, Status);
        Control(dialog, L"BUTTON", L"立即重新连接", 18, 303, 108, 30, Reconnect, WS_TABSTOP);
        Control(dialog, L"BUTTON", L"使用说明", 138, 303, 85, 30, Help, WS_TABSTOP);
        Control(dialog, L"BUTTON", L"保存并连接", width - 205, 303, 110, 30, IDOK, WS_TABSTOP | BS_DEFPUSHBUTTON);
        Control(dialog, L"BUTTON", L"取消", width - 83, 303, 65, 30, IDCANCEL, WS_TABSTOP);
        SetTimer(dialog, 1, 500, nullptr);
        return TRUE;
    }
    if (!state) return FALSE;
    if (message == WM_TIMER) {
        SetDlgItemTextW(dialog, Status, state->runtime->Latest().status.c_str());
        return TRUE;
    }
    if (message == WM_CLOSE) { EndDialog(dialog, IDCANCEL); return TRUE; }
    if (message != WM_COMMAND) return FALSE;
    try {
        switch (LOWORD(wp)) {
        case IDCANCEL: EndDialog(dialog, IDCANCEL); return TRUE;
        case Browse: {
            wchar_t file[32768]{};
            OPENFILENAMEW picker{sizeof(picker)};
            picker.hwndOwner = dialog;
            picker.lpstrFilter = L"YAML 配置\0*.yaml;*.yml\0所有文件\0*.*\0";
            picker.lpstrFile = file; picker.nMaxFile = 32768;
            picker.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&picker)) SetDlgItemTextW(dialog, Config, file);
            return TRUE;
        }
        case Reconnect: state->runtime->Reconnect(); SetDlgItemTextW(dialog, Status, L"正在重新连接…"); return TRUE;
        case Help:
            MessageBoxW(dialog,
                L"在 TrafficMonitor 的任务栏“显示项目”中选择 FlClash 上传、FlClash 下载。\n"
                L"字体、颜色、透明背景和排列由 TrafficMonitor 统一设置。\n\n"
                L"小于 10 保留两位小数；10～99 保留一位；100 及以上最多两位。\n"
                L"达到 1024 KB/s 切换为 MB/s。未连接显示 —，每 3 秒自动重连。\n\n"
                L"统计 FlClash 内核处理的流量，包含 DIRECT 直连。\n"
                L"“立即重新连接”是点击操作，不需要勾选。",
                L"FlClashSpeed 插件", MB_OK | MB_ICONINFORMATION);
            return TRUE;
        case IDOK: {
            Settings settings{Text(dialog, Config), Text(dialog, Endpoint), Text(dialog, Secret)};
            if (!settings.endpoint.empty()) ParseEndpoint(settings.endpoint, settings.secret);
            if (!settings.configPath.empty() && GetFileAttributesW(settings.configPath.c_str()) == INVALID_FILE_ATTRIBUTES)
                throw std::runtime_error("指定的配置文件不存在");
            state->runtime->UpdateSettings(settings);
            state->changed = true;
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        }
    } catch (const std::exception& error) {
        MessageBoxW(dialog, Widen(error.what()).c_str(), L"FlClashSpeed 插件", MB_OK | MB_ICONWARNING);
        return TRUE;
    }
    return FALSE;
}
}
bool ShowSettingsDialog(Runtime& runtime, HWND parent) {
    // Build a minimal UTF-16 dialog template; controls are standard Win32 controls.
    std::vector<WORD> words(sizeof(DLGTEMPLATE) / sizeof(WORD), 0);
    auto append = [&](const wchar_t* value) { while (*value) words.push_back(static_cast<WORD>(*value++)); words.push_back(0); };
    words.push_back(0); words.push_back(0);
    append(L"FlClashSpeed · 连接选项");
    words.push_back(9); append(L"Microsoft YaHei UI");
    auto* specification = reinterpret_cast<DLGTEMPLATE*>(words.data());
    specification->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | DS_CENTER;
    specification->cx = 340; specification->cy = 240;
    DialogState state{&runtime};
    auto result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), specification, parent, DialogProc, reinterpret_cast<LPARAM>(&state));
    if (result == -1) MessageBoxW(parent, L"无法创建插件选项窗口", L"FlClashSpeed 插件", MB_ICONWARNING);
    return state.changed;
}
}
