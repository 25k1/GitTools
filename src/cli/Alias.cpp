#include "cli/Alias.hpp"

#include "cli/Util.hpp"
#include "git/Git.hpp"

#include <algorithm>
#include <string>

namespace git_tools {

namespace {

struct AliasSpec {
    const wchar_t* name;
    const wchar_t* subcommand;
};

constexpr AliasSpec kAliases[] = {
    {L"pl", L"pull-log"},
    {L"lg", L"log"},
    {L"br", L"branch"},
};

bool SetAlias(const std::wstring& name, const std::wstring& value) {
    ProcessResult r = RunGit({L"config", L"--global", L"alias." + name, value});
    if (r.ok()) return true;
    WriteErr(r.started ? L"git config failed for alias." + name + L":\n" +
                             Utf8ToWide(TrimRight(r.stderrText))
                       : L"Failed to launch git: " + r.errorMessage);
    return false;
}

}

int RunInstallAlias() {
    SetConsoleOutputCP(CP_UTF8);

    std::wstring exe = ExecutablePath();
    if (exe.empty()) {
        WriteErr(L"Failed to resolve gittools.exe path.");
        return 1;
    }
    std::ranges::replace(exe, L'\\', L'/');

    bool ok = true;
    for (const AliasSpec& a : kAliases) {
        ok = SetAlias(a.name, L"!\"" + exe + L"\" " + a.subcommand) && ok;
    }
    if (!ok) return 1;

    WriteOut(L"Installed git aliases (--global):");
    for (const AliasSpec& a : kAliases) {
        WriteOut(std::wstring(L"  git ") + a.name + L"  ->  gittools " +
                 a.subcommand);
    }
    WriteOut(L"");
    WriteOut(L"Remove with:  gittools uninstall-alias");
    return 0;
}

int RunUninstallAlias() {
    SetConsoleOutputCP(CP_UTF8);
    for (const AliasSpec& a : kAliases) {
        RunGit({L"config", L"--global", L"--unset", std::wstring(L"alias.") + a.name});
    }
    WriteOut(L"Removed git aliases (if present):");
    for (const AliasSpec& a : kAliases) WriteOut(std::wstring(L"  ") + a.name);
    return 0;
}

}
