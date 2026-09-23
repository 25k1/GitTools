#include "git/Config.hpp"

#include "git/Git.hpp"

#include <map>

namespace git_tools {

namespace {

constexpr std::wstring_view kSection = L"gittools.";

std::map<std::wstring, std::wstring>& Cache() {
    static std::map<std::wstring, std::wstring> cache = [] {
        std::map<std::wstring, std::wstring> entries;
        ProcessResult r = RunGit(
            {L"config", L"--global", L"--get-regexp", L"^gittools\\."});
        if (!r.ok()) return entries;
        ForEachLine(Utf8ToWide(r.stdoutText), [&](std::wstring_view raw) {
            const std::wstring line = TrimRight(raw);
            if (!line.starts_with(kSection)) return;
            const size_t space = line.find(L' ');
            const std::wstring name =
                line.substr(kSection.size(), space == line.npos
                                                 ? line.npos
                                                 : space - kSection.size());
            entries[ToLower(name)] =
                space == line.npos ? std::wstring() : line.substr(space + 1);
        });
        return entries;
    }();
    return cache;
}

}

std::wstring ConfigGet(const std::wstring& key, const std::wstring& fallback) {
    const auto it = Cache().find(ToLower(key));
    return (it == Cache().end() || it->second.empty()) ? fallback : it->second;
}

bool ConfigGetBool(const std::wstring& key, bool fallback) {
    const std::wstring v = ToLower(ConfigGet(key));
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
    Cache()[ToLower(key)] = value;
    RunGit({L"config", L"--global", std::wstring(kSection) + key, value});
}

void ConfigSetBool(const std::wstring& key, bool value) {
    ConfigSet(key, value ? L"true" : L"false");
}

}
