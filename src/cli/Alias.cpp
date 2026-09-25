#include "cli/Alias.hpp"

#include "git/Config.hpp"
#include "git/Git.hpp"
#include "util/System.hpp"

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
    const ProcessResult r = GlobalConfigSet(L"alias." + name, value);
    if (r.ok()) return true;
    WriteErr(GitFailure(L"git config alias." + name, r));
    return false;
}

}

int RunInstallAlias() {
    UseUtf8Console();

    bool ok = true;
    for (const AliasSpec& a : kAliases) {
        const std::wstring command = SelfCommand(a.subcommand);
        if (command.empty()) {
            WriteErr(L"Failed to resolve the gittools executable path.");
            return 1;
        }
        ok = SetAlias(a.name, L"!" + command) && ok;
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
    UseUtf8Console();
    for (const AliasSpec& a : kAliases) {
        GlobalConfigUnset(std::wstring(L"alias.") + a.name);
    }
    WriteOut(L"Removed git aliases (if present):");
    for (const AliasSpec& a : kAliases) WriteOut(std::wstring(L"  ") + a.name);
    return 0;
}

}
