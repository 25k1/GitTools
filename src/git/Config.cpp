#include "git/Config.hpp"

#include "git/Git.hpp"

#include <map>

namespace git_tools {

namespace {

constexpr wchar_t kSection[] = L"gittools.";

std::map<std::wstring, std::wstring>& Cache() {
    static std::map<std::wstring, std::wstring> m;
    return m;
}

std::wstring Lower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
    return s;
}

void EnsureLoaded() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;

    ProcessResult r = RunGit(
        {L"config", L"--global", L"--get-regexp", L"^gittools\\."});
    if (!r.started || r.exitCode != 0) return;

    std::wstring text = Utf8ToWide(r.stdoutText);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find(L'\n', pos);
        size_t end = (eol == std::wstring::npos) ? text.size() : eol;
        std::wstring line = text.substr(pos, end - pos);
        pos = (eol == std::wstring::npos) ? text.size() : eol + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) {
            line.pop_back();
        }
        if (line.rfind(kSection, 0) != 0) continue;

        size_t space = line.find(L' ');
        std::wstring name = line.substr(0, space);
        std::wstring value =
            (space == std::wstring::npos) ? std::wstring()
                                          : line.substr(space + 1);
        Cache()[Lower(name.substr(wcslen(kSection)))] = value;
    }
}
}

std::wstring ConfigGet(const std::wstring& key, const std::wstring& fallback) {
    EnsureLoaded();
    auto it = Cache().find(Lower(key));
    return (it == Cache().end() || it->second.empty()) ? fallback : it->second;
}

bool ConfigGetBool(const std::wstring& key, bool fallback) {
    std::wstring v = Lower(ConfigGet(key));
    if (v == L"true" || v == L"yes" || v == L"on" || v == L"1") return true;
    if (v == L"false" || v == L"no" || v == L"off" || v == L"0") return false;
    return fallback;
}

int ConfigGetInt(const std::wstring& key, int fallback) {
    const std::wstring v = ConfigGet(key);
    if (v.empty()) return fallback;
    int value = 0;
    for (wchar_t c : v) {
        if (c < L'0' || c > L'9') return fallback;
        value = value * 10 + (c - L'0');
        if (value > 1000000) return fallback;
    }
    return value;
}

void ConfigSet(const std::wstring& key, const std::wstring& value) {
    EnsureLoaded();
    Cache()[Lower(key)] = value;
    RunGit({L"config", L"--global", kSection + key, value});
}

void ConfigSetBool(const std::wstring& key, bool value) {
    ConfigSet(key, value ? L"true" : L"false");
}

}
