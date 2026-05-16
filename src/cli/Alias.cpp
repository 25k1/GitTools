#include "cli/Alias.hpp"

#include "cli/Util.hpp"
#include "git/Git.hpp"
#include "ui/Encoding.hpp"

#include <iterator>
#include <string>

namespace git_tools {

namespace {

std::wstring NormalizedExePath() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(
        nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) return {};
    std::wstring path(buf, n);
    for (auto& c : path) if (c == L'\\') c = L'/';
    return path;
}

void WriteOut(const std::wstring& s) {
    WriteConsoleLine(GetStdHandle(STD_OUTPUT_HANDLE), s);
}

void WriteErr(const std::wstring& s) {
    WriteConsoleLine(GetStdHandle(STD_ERROR_HANDLE), s);
}

bool SetAlias(const std::wstring& name, const std::wstring& value) {
    ProcessResult r = RunGit(
        {L"config", L"--global", L"alias." + name, value});
    if (!r.started) {
        WriteErr(L"Failed to launch git: " + r.errorMessage);
        return false;
    }
    if (r.exitCode != 0) {
        WriteErr(L"git config failed for alias." + name + L":\n" +
                 Utf8ToWide(RStrip(r.stderrText)));
        return false;
    }
    return true;
}

void UnsetAlias(const std::wstring& name) {
    RunGit({L"config", L"--global", L"--unset", L"alias." + name});
}

struct AliasSpec {
    const wchar_t* name;
    const wchar_t* subcommand;
};

constexpr AliasSpec kAliases[] = {
    {L"pl", L"pull-log"},
    {L"lg", L"log"},
    {L"br", L"branch"},
};

}

int RunInstallAlias() {
    SetConsoleOutputCP(CP_UTF8);

    const std::wstring exe = NormalizedExePath();
    if (exe.empty()) {
        WriteErr(L"Failed to resolve gittools.exe path.");
        return 1;
    }

    bool ok = true;
    for (const auto& a : kAliases) {
        std::wstring value = L"!\"";
        value += exe;
        value += L"\" ";
        value += a.subcommand;
        ok = SetAlias(a.name, value) && ok;
    }
    if (!ok) return 1;

    WriteOut(L"Installed git aliases (--global):");
    for (const auto& a : kAliases) {
        std::wstring line = L"  git ";
        line += a.name;
        line += L"  ->  gittools ";
        line += a.subcommand;
        WriteOut(line);
    }
    WriteOut(L"");
    WriteOut(L"Remove with:  gittools uninstall-alias");
    return 0;
}

int RunUninstallAlias() {
    SetConsoleOutputCP(CP_UTF8);
    for (const auto& a : kAliases) UnsetAlias(a.name);
    WriteOut(L"Removed git aliases (if present):");
    for (const auto& a : kAliases) {
        std::wstring line = L"  ";
        line += a.name;
        WriteOut(line);
    }
    return 0;
}

}
