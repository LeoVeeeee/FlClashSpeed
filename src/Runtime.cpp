#include "Core.h"
#include <stdexcept>
#include <utility>
#include <algorithm>

namespace flclash {
struct InternetHandle {
    HINTERNET value{};
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
};
Runtime::Runtime() {
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}
void Runtime::Start() {
    if (!stop_ || !wake_) {
        Publish({0, 0, false, 0, L"无法创建后台同步事件"});
        return;
    }
    if (started_.exchange(true)) return;
    // TrafficMonitor owns plugins until process exit. Pin the module before starting
    // background work: FreeLibrary cannot unmap callbacks during host shutdown.
    // No waiting, thread creation, or network work occurs inside DllMain.
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&Runtime::Worker), &module)) {
        started_ = false; return;
    }
    thread_ = CreateThread(nullptr, 0, Worker, this, 0, nullptr);
    if (!thread_) { started_ = false; Publish({0, 0, false, 0, L"后台线程启动失败"}); }
}
void Runtime::Stop() {
    SetEvent(stop_);
    // Synchronous WinHTTP handles belong exclusively to the worker. Wait for
    // its bounded HTTP timeout instead of closing a handle during a read.
    if (thread_) { WaitForSingleObject(thread_, INFINITE); CloseHandle(thread_); thread_ = nullptr; }
}
void Runtime::Reconnect() {
    generation_.fetch_add(1);
    Publish({0, 0, false, 0, L"正在重新连接 FlClash…"});
    SetEvent(wake_);
    Start();
}
void Runtime::ConfigureFolder(const wchar_t* folder) {
    if (!folder || !*folder) return;
    std::wstring directory(folder);
    if (directory.back() != L'\\' && directory.back() != L'/') directory += L'\\';
    std::wstring path = directory + L"FlClashSpeedPlugin.ini";
    Settings loaded;
    std::wstring error;
    try { loaded = LoadSettings(path); }
    catch (const std::exception&) { error = L"插件配置或保存的密钥损坏，请打开连接选项重新保存"; }
    AcquireSRWLockExclusive(&lock_);
    settingsFile_ = path;
    settings_ = std::move(loaded);
    settingsError_ = error;
    ReleaseSRWLockExclusive(&lock_);
    if (started_) Reconnect();
    if (!error.empty()) Publish({0, 0, false, 0, error});
}
Settings Runtime::GetSettings() {
    AcquireSRWLockShared(&lock_);
    Settings settings = settings_;
    ReleaseSRWLockShared(&lock_);
    return settings;
}
void Runtime::UpdateSettings(const Settings& settings) {
    AcquireSRWLockShared(&lock_);
    std::wstring path = settingsFile_;
    ReleaseSRWLockShared(&lock_);
    if (path.empty()) throw std::runtime_error("宿主尚未提供插件配置目录");
    SaveSettings(path, settings);
    AcquireSRWLockExclusive(&lock_);
    settings_ = settings;
    settingsError_.clear();
    ReleaseSRWLockExclusive(&lock_);
    Reconnect();
}
Snapshot Runtime::Latest() {
    AcquireSRWLockShared(&lock_);
    Snapshot sample = latest_;
    ReleaseSRWLockShared(&lock_);
    if (sample.connected && GetTickCount64() - sample.received > 5000) {
        sample.connected = false;
        sample.status = L"流量数据超时，正在自动重连…";
    }
    return sample;
}
void Runtime::Publish(Snapshot sample) {
    AcquireSRWLockExclusive(&lock_);
    latest_ = std::move(sample);
    ReleaseSRWLockExclusive(&lock_);
}
void Runtime::PublishIfCurrent(Snapshot sample, uint64_t generation) {
    AcquireSRWLockExclusive(&lock_);
    if (generation == generation_) latest_ = std::move(sample);
    ReleaseSRWLockExclusive(&lock_);
}
DWORD WINAPI Runtime::Worker(void* context) {
    static_cast<Runtime*>(context)->Run();
    return 0;
}
void Runtime::Run() {
    HANDLE events[] = {stop_, wake_};
    while (WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0) {
        const auto generation = generation_.load();
        try {
            AcquireSRWLockShared(&lock_);
            auto error = settingsError_;
            ReleaseSRWLockShared(&lock_);
            if (!error.empty()) throw std::runtime_error(Narrow(error));
            Stream(Discover(GetSettings()), generation);
        }
        catch (const std::exception& error) {
            if (generation == generation_ && WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0)
                PublishIfCurrent({0, 0, false, 0, Widen(error.what())}, generation);
        }
        catch (...) { PublishIfCurrent({0, 0, false, 0, L"读取流量失败，正在自动重连…"}, generation); }
        if (generation != generation_) continue;
        if (WaitForMultipleObjects(2, events, FALSE, 3000) == WAIT_OBJECT_0) break;
    }
}
void Runtime::Stream(const Controller& controller, uint64_t generation) {
    InternetHandle session{WinHttpOpen(L"FlClashSpeedPlugin/2.0.1", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) throw std::runtime_error("无法初始化 HTTP 客户端");
    if (!WinHttpSetTimeouts(session.value, 2000, 2000, 3000, 5000))
        throw std::runtime_error("无法设置 HTTP 超时");
    InternetHandle connection{WinHttpConnect(session.value, controller.host.c_str(), controller.port, 0)};
    if (!connection.value) throw std::runtime_error("无法连接本机控制器，正在重试…");
    InternetHandle requestHandle{WinHttpOpenRequest(connection.value, L"GET", L"/traffic", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, controller.secure ? WINHTTP_FLAG_SECURE : 0)};
    HINTERNET request = requestHandle.value;
    if (!request) throw std::runtime_error("无法创建流量请求");
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)))
        throw std::runtime_error("无法禁用控制器重定向");
    auto cancelled = [&] { return generation != generation_ || WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0; };
    if (cancelled()) return;
    std::wstring header = controller.secret.empty() ? L"" : L"Authorization: Bearer " + controller.secret + L"\r\n";
    if (!WinHttpSendRequest(request, header.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : header.c_str(),
            static_cast<DWORD>(header.size()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr))
        throw std::runtime_error("无法连接 FlClash；请确认外部控制器已开启，正在重试…");
    DWORD status{}, size = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("读取控制器响应失败");
    if (status == 401 || status == 403) throw std::runtime_error("控制器密钥不匹配；请检查插件连接选项");
    if (status != 200) throw std::runtime_error("控制器返回错误；请检查地址及端口");
    std::string pending;
    char buffer[4096];
    auto lastRecord = GetTickCount64();
    while (!cancelled()) {
        DWORD available{};
        if (!WinHttpQueryDataAvailable(request, &available))
            throw std::runtime_error("流量数据超时或连接断开，正在自动重连…");
        if (!available) throw std::runtime_error("FlClash 已断开，正在自动重连…");
        DWORD count{};
        if (!WinHttpReadData(request, buffer, (std::min)(available, static_cast<DWORD>(sizeof(buffer))), &count))
            throw std::runtime_error("流量数据超时或连接断开，正在自动重连…");
        if (!count) throw std::runtime_error("FlClash 已断开，正在自动重连…");
        pending.append(buffer, count);
        if (GetTickCount64() - lastRecord > 5000)
            throw std::runtime_error("流量数据超时，正在自动重连…");
        if (pending.size() > 65536) throw std::runtime_error("流量响应过大");
        size_t end;
        while ((end = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, end);
            pending.erase(0, end + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && !cancelled()) {
                PublishIfCurrent(ParseTraffic(line), generation);
                lastRecord = GetTickCount64();
            }
        }
    }
}
}
