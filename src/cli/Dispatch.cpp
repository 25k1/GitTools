#include "cli/Dispatch.hpp"

#include "Version.h"
#include "cli/Alias.hpp"
#include "cli/Branches.hpp"
#include "cli/Detached.hpp"
#include "cli/Log.hpp"
#include "cli/PullLog.hpp"
#include "cli/Util.hpp"
#include "git/Git.hpp"
#include "ui/Encoding.hpp"
#include "ui/LogWindow.hpp"

#include <string>
#include <string_view>

namespace git_tools {

namespace {

constexpr wchar_t kTitle[] = L"gittools";

int RunTestGit() {
    ProcessResult r = RunGit({L"--version"});
    std::wstring text;
    if (!r.started) {
        text = L"Failed to launch git:\n\n" + r.errorMessage;
    } else {
        text  = L"git --version (exit ";
        text += std::to_wstring(r.exitCode);
        text += L")\n\n";
        text += L"stdout:\n";
        text += Utf8ToWide(r.stdoutText);
        if (!r.stderrText.empty()) {
            text += L"\nstderr:\n";
            text += Utf8ToWide(r.stderrText);
        }
    }
    MessageBoxW(nullptr, text.c_str(), L"gittools test-git",
                MB_OK | MB_ICONINFORMATION);
    return (r.started && r.exitCode == 0) ? 0 : 1;
}

int RunVersion() {
    SetConsoleOutputCP(CP_UTF8);
    WriteConsoleLine(GetStdHandle(STD_OUTPUT_HANDLE),
                     std::wstring(L"gittools ") + kVersion);
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
    MessageBoxW(nullptr, usage, kTitle, MB_OK | MB_ICONINFORMATION);
    return 0;
}

int RunLogRange(int argc, wchar_t** argv) {
    constexpr wchar_t kRangeTitle[] = L"gittools log-range";
    if (argc < 4) {
        ReportDialogError(kRangeTitle, L"usage: gittools log-range OLD NEW");
        return 1;
    }
    const std::wstring oldSha = argv[2];
    const std::wstring newSha = argv[3];

    RepoContext repo;
    if (!OpenRepoOrReport(kRangeTitle, repo)) return 1;

    CommitListResult lr = LoadCommitRange(oldSha, newSha, repo.cwd);
    if (!lr.errorMessage.empty()) {
        ReportDialogError(kRangeTitle, lr.errorMessage);
        return 1;
    }

    return ShowLogWindow(repo, oldSha + L".." + newSha,
                         RangeLogArgs(oldSha, newSha),
                         std::move(lr.commits));
}

}

int Dispatch(int argc, wchar_t** argv) {
    const std::wstring_view cmd =
        (argc >= 2) ? std::wstring_view{argv[1]} : std::wstring_view{};

    const bool spawnsDialog =
        cmd == L"pull-log" || cmd == L"log" || cmd == L"branch";
    const bool writesConsole =
        cmd == L"install-alias" || cmd == L"uninstall-alias" ||
        cmd == L"--version" || cmd == L"-v";

    const bool keepConsole =
        writesConsole ||
        (spawnsDialog && !IsDetachedInvocation(argc, argv));
    if (!keepConsole) FreeConsole();

    if (cmd == L"--version" ||
        cmd == L"-v")              return RunVersion();
    if (cmd == L"test-git")        return RunTestGit();
    if (cmd == L"pull-log")        return RunPullLog(argc, argv);
    if (cmd == L"log")             return RunLog(argc, argv);
    if (cmd == L"log-range")       return RunLogRange(argc, argv);
    if (cmd == L"branch")          return RunBranch(argc, argv);
    if (cmd == L"install-alias")   return RunInstallAlias();
    if (cmd == L"uninstall-alias") return RunUninstallAlias();
    return RunUsage();
}

}
