#include "Core.h"
#include <wincrypt.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <limits>
#include <cwctype>
#include "../third_party/json.hpp"

namespace flclash {
using Json = nlohmann::json;

std::wstring Widen(const std::string& value) {
    if (value.empty()) return {};
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!length) throw std::runtime_error("文件不是有效 UTF-8");
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length);
    return result;
}
std::string Narrow(const std::wstring& value) {
    if (value.empty()) return {};
    int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}
std::string ReadUtf8File(const std::wstring& path, size_t maxBytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("无法读取配置文件");
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || static_cast<uint64_t>(size.QuadPart) > maxBytes) {
        CloseHandle(file); throw std::runtime_error("配置文件过大");
    }
    std::string result(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read{};
    bool success = ReadFile(file, result.data(), static_cast<DWORD>(result.size()), &read, nullptr);
    CloseHandle(file);
    if (!success || read != result.size()) throw std::runtime_error("配置文件读取失败");
    if (result.rfind("\xef\xbb\xbf", 0) == 0) result.erase(0, 3);
    return result;
}

std::wstring FormatSpeed(uint64_t bytes) {
    double value = static_cast<double>(bytes) / 1024;
    const wchar_t* unit = L" KB/s";
    if (value >= 1024) { value /= 1024; unit = L" MB/s"; }
    int precision = value < 10 ? 2 : value < 100 ? 1 : 2;
    double factor = precision == 1 ? 10 : 100;
    value = std::round(value * factor) / factor;
    precision = value < 10 ? 2 : value < 100 ? 1 : 2;
    std::wostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(precision) << value;
    std::wstring text = output.str();
    if (value >= 100 && text.find(L'.') != std::wstring::npos) {
        while (text.back() == L'0') text.pop_back();
        if (text.back() == L'.') text.pop_back();
    }
    return text + unit;
}

static std::string Trim(std::string value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    return value.substr(start, value.find_last_not_of(" \t\r\n") - start + 1);
}
static std::string YamlScalar(std::string value) {
    value = Trim(value);
    if (value.empty()) return {};
    if (value.front() == '"') {
        bool escaped = false;
        for (size_t i = 1; i < value.size(); ++i) {
            if (!escaped && value[i] == '"') {
                try { return Json::parse(value.substr(0, i + 1)).get<std::string>(); }
                catch (const std::exception&) { throw std::runtime_error("配置中的双引号字符串无效"); }
            }
            if (value[i] == '\\' && !escaped) escaped = true; else escaped = false;
        }
        throw std::runtime_error("配置中的引号未闭合");
    }
    if (value.front() == '\'') {
        std::string out;
        for (size_t i = 1; i < value.size(); ++i) {
            if (value[i] != '\'') out += value[i];
            else if (i + 1 < value.size() && value[i + 1] == '\'') { out += '\''; ++i; }
            else return out;
        }
        throw std::runtime_error("配置中的引号未闭合");
    }
    for (size_t i = 0; i < value.size(); ++i)
        if (value[i] == '#' && (i == 0 || value[i - 1] == ' ' || value[i - 1] == '\t')) return Trim(value.substr(0, i));
    return value;
}
static std::wstring EnvFolder(const wchar_t* name) {
    DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size) return {};
    std::wstring value(size, L'\0');
    GetEnvironmentVariableW(name, value.data(), size);
    value.resize(size - 1);
    return value;
}
static bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

Controller ParseEndpoint(const std::wstring& endpoint, const std::wstring& secret) {
    std::wstring address = endpoint;
    if (address.find(L"://") == std::wstring::npos) address = L"http://" + address;
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = components.dwUrlPathLength = components.dwExtraInfoLength =
        components.dwUserNameLength = components.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(address.c_str(), static_cast<DWORD>(address.size()), 0, &components))
        throw std::runtime_error("控制器地址格式错误，例如 http://127.0.0.1:9090");
    std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::transform(host.begin(), host.end(), host.begin(), [](wchar_t c) { return std::towlower(c); });
    std::wstring path = components.dwUrlPathLength ? std::wstring(components.lpszUrlPath, components.dwUrlPathLength) : L"";
    if (components.dwUserNameLength || components.dwPasswordLength || components.dwExtraInfoLength ||
        (!path.empty() && path != L"/") || (components.nScheme != INTERNET_SCHEME_HTTP && components.nScheme != INTERNET_SCHEME_HTTPS))
        throw std::runtime_error("请填写本机 HTTP/HTTPS 控制器地址，不要包含路径或账号");
    if (host == L"0.0.0.0" || host == L"localhost") host = L"127.0.0.1";
    if (host == L"[::]" || host == L"::") host = L"::1";
    if (host != L"127.0.0.1" && host != L"::1" && host != L"[::1]")
        throw std::runtime_error("只允许连接本机控制器（127.0.0.1 或 localhost）");
    if (secret.find_first_of(L"\r\n") != std::wstring::npos) throw std::runtime_error("密钥不能包含换行");
    return {host, components.nPort, components.nScheme == INTERNET_SCHEME_HTTPS, secret};
}
Controller Discover(const Settings& settings) {
    if (!settings.endpoint.empty()) return ParseEndpoint(settings.endpoint, settings.secret);
    std::vector<std::wstring> paths;
    if (!settings.configPath.empty()) paths.push_back(settings.configPath);
    else for (const wchar_t* env : {L"APPDATA", L"LOCALAPPDATA"})
        for (const wchar_t* folder : {L"clash", L"flclash"})
            paths.push_back(EnvFolder(env) + L"\\com.follow\\" + folder + L"\\config.yaml");
    for (const auto& file : paths) {
        if (!Exists(file)) continue;
        std::string endpoint, secret;
        std::istringstream lines(ReadUtf8File(file));
        std::string line;
        while (std::getline(lines, line)) {
            if (line.rfind("external-controller:", 0) == 0) endpoint = YamlScalar(line.substr(20));
            else if (line.rfind("secret:", 0) == 0) secret = YamlScalar(line.substr(7));
        }
        const auto separator = file.find_last_of(L"\\/");
        std::wstring preferences = file.substr(0, separator + 1) + L"shared_preferences.json";
        if (Exists(preferences)) {
            try {
                auto outer = Json::parse(ReadUtf8File(preferences));
                if (outer.contains("flutter.config") && outer["flutter.config"].is_string()) {
                    auto config = Json::parse(outer["flutter.config"].get<std::string>());
                    if (config.contains("patchClashConfig") && config["patchClashConfig"].is_object()) {
                        auto& patch = config["patchClashConfig"];
                        if (patch.contains("external-controller") && patch["external-controller"].is_string()) endpoint = patch["external-controller"].get<std::string>();
                        if (patch.contains("secret") && patch["secret"].is_string()) secret = patch["secret"].get<std::string>();
                    }
                }
            } catch (...) { /* The YAML remains usable while FlClash saves preferences. */ }
        }
        if (endpoint.empty()) endpoint = "127.0.0.1:9090";
        return ParseEndpoint(Widen(endpoint), Widen(secret));
    }
    throw std::runtime_error("未找到 FlClash 配置；请在插件选项中指定配置文件");
}
Snapshot ParseTraffic(const std::string& line) {
    Json json;
    try { json = Json::parse(line); }
    catch (const std::exception&) { throw std::runtime_error("流量数据不是有效 JSON"); }
    if (!json.is_object() || !json.contains("up") || !json.contains("down"))
        throw std::runtime_error("流量数据缺少上传或下载计数");
    auto counter = [&](const char* key) -> uint64_t {
        const auto& value = json.at(key);
        if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0))
            throw std::runtime_error("流量数据无效");
        return value.get<uint64_t>();
    };
    return {counter("up"), counter("down"), true, GetTickCount64(), L"已连接 FlClash"};
}

static std::wstring IniValue(const std::wstring& file, const wchar_t* key) {
    std::vector<wchar_t> buffer(8192);
    GetPrivateProfileStringW(L"connection", key, L"", buffer.data(), static_cast<DWORD>(buffer.size()), file.c_str());
    return buffer.data();
}
static std::wstring Protect(const std::wstring& plain) {
    if (plain.empty()) return {};
    auto bytes = Narrow(plain);
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())}, output{};
    if (!CryptProtectData(&input, L"FlClashSpeedPlugin", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        throw std::runtime_error("密钥加密失败，设置未保存");
    std::wstring text;
    const wchar_t* hex = L"0123456789abcdef";
    for (DWORD i = 0; i < output.cbData; ++i) { text += hex[output.pbData[i] >> 4]; text += hex[output.pbData[i] & 15]; }
    LocalFree(output.pbData);
    return text;
}
static std::wstring Unprotect(const std::wstring& text) {
    if (text.empty()) return {};
    if (text.size() % 2) throw std::runtime_error("保存的密钥无效，请重新输入");
    std::vector<BYTE> bytes;
    auto digit = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        throw std::runtime_error("保存的密钥无效，请重新输入");
    };
    for (size_t i = 0; i < text.size(); i += 2) bytes.push_back(static_cast<BYTE>((digit(text[i]) << 4) | digit(text[i + 1])));
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), bytes.data()}, output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        throw std::runtime_error("保存的密钥属于其他 Windows 用户，请重新输入");
    std::string plain(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return Widen(plain);
}
Settings LoadSettings(const std::wstring& path) {
    return {IniValue(path, L"config_path"), IniValue(path, L"endpoint"), Unprotect(IniValue(path, L"secret_dpapi"))};
}
void SaveSettings(const std::wstring& path, const Settings& settings) {
    if (settings.configPath.find_first_of(L"\r\n") != std::wstring::npos ||
        settings.endpoint.find_first_of(L"\r\n") != std::wstring::npos)
        throw std::runtime_error("配置路径和地址不能包含换行");
    if (settings.configPath.size() > 4096 || settings.endpoint.size() > 2048)
        throw std::runtime_error("配置路径或地址过长");
    if (!settings.endpoint.empty()) ParseEndpoint(settings.endpoint, settings.secret);
    auto secret = Protect(settings.secret);
    if (secret.size() > 8000) throw std::runtime_error("密钥过长，设置未保存");
    std::wstring content = L"\xfeff[connection]\r\nconfig_path=" + settings.configPath +
        L"\r\nendpoint=" + settings.endpoint + L"\r\nsecret_dpapi=" + secret + L"\r\n";
    static std::atomic<uint64_t> sequence{};
    std::wstring temporary = path + L".tmp." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(sequence.fetch_add(1));
    // Replace a complete UTF-16 file atomically; failure preserves old settings.
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("无法创建插件配置文件，请检查目录权限");
    DWORD count{};
    DWORD bytes = static_cast<DWORD>(content.size() * sizeof(wchar_t));
    bool success = WriteFile(file, content.data(), bytes, &count, nullptr) && count == bytes && FlushFileBuffers(file);
    CloseHandle(file);
    if (!success || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("无法保存插件设置，请检查配置目录权限");
    }
}
}
