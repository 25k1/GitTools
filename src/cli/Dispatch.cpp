#include "cli/Dispatch.hpp"

#include "Version.h"
#include "cli/Alias.hpp"
#include "cli/Branches.hpp"
#include "cli/Detached.hpp"
#include "cli/Log.hpp"
#include "cli/PullLog.hpp"
#include "git/Git.hpp"
#include "ui/App.hpp"
#include "util/System.hpp"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

namespace git_tools {

namespace {

int RunTestGit(int, wchar_t**) {
    ProcessResult r = RunGit({L"--version"});
    std::wstring text;
    if (!r.started) {
        text = L"Failed to launch git:\n\n" + r.errorMessage;
    } else {
        text = L"git --version (exit " + std::to_wstring(r.exitCode) +
               L")\n\nstdout:\n" + Utf8ToWide(r.stdoutText);
        if (!r.stderrText.empty()) {
            text += L"\nstderr:\n" + Utf8ToWide(r.stderrText);
        }
    }
    RunGui([&] {
        ShowInfo(nullptr, L"gittools test-git", text);
        return 0;
    });
    return r.ok() ? 0 : 1;
}

int RunVersion(int, wchar_t**) {
    UseUtf8Console();
    WriteOut(std::wstring(L"gittools ") + kVersion);
    return 0;
}

int RunUsage() {
    const wchar_t* usage =
        L"gittools " GITTOOLS_VERSION_STR L" - usage:\n\n"
        L"  gittools test-git              run `git --version` (diagnostic)\n"
        L"  gittools pull-log [args]       run `git pull` and summarise\n"
        L"  gittools log [args]            open the log window for any git-log args\n"
        L"  gittools log-range OLD NEW     open the log window for OLD..NEW\n"
        L"  gittools branch                open the branch switcher\n"
        L"  gittools install-alias         add `git pl` / `git lg` / `git br` to ~/.gitconfig\n"
        L"  gittools uninstall-alias       remove those aliases\n"
        L"  gittools --version, -v         print the version\n";
    return RunGui([usage] {
        ShowInfo(nullptr, L"gittools", usage);
        return 0;
    });
}

enum class Console {
    Free,
    Keep,
    KeepUntilDetached,
};

struct Command {
    std::wstring_view name;
    int (*run)(int, wchar_t**);
    Console console;
};

constexpr Command kCommands[] = {
    {L"--version",       RunVersion,  Console::Keep},
    {L"-v",              RunVersion,  Console::Keep},
    {L"test-git",        RunTestGit,  Console::Free},
    {L"pull-log",        RunPullLog,  Console::KeepUntilDetached},
    {L"log",             RunLog,      Console::KeepUntilDetached},
    {L"log-range",       RunLogRange, Console::Free},
    {L"branch",          RunBranch,   Console::KeepUntilDetached},
    {L"install-alias",   [](int, wchar_t**) { return RunInstallAlias(); },
                         Console::Keep},
    {L"uninstall-alias", [](int, wchar_t**) { return RunUninstallAlias(); },
                         Console::Keep},
};

}

int Dispatch(int argc, wchar_t** argv) {
    const std::wstring_view name =
        (argc >= 2) ? std::wstring_view{argv[1]} : std::wstring_view{};
    const auto command = std::ranges::find(kCommands, name, &Command::name);
    const bool found   = command != std::end(kCommands);

    const Console console = found ? command->console : Console::Free;
    const bool keepConsole =
        console == Console::Keep ||
        (console == Console::KeepUntilDetached &&
         !IsDetachedInvocation(argc, argv));
    if (!keepConsole) ReleaseConsole();

    return found ? command->run(argc, argv) : RunUsage();
}

}
