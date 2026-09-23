#include "cli/PullLog.hpp"

#include "cli/Detached.hpp"
#include "git/Git.hpp"
#include "ui/LogWindow.hpp"

#include <windows.h>

#include <string>
#include <vector>

namespace git_tools {

namespace {

int RunPullLogInChild(const std::wstring& oldSha, const std::wstring& newSha) {
    RepoContext repo;
    if (!OpenRepoOrReport(L"gittools pull-log", repo)) return 1;

    CommitListResult lr =
        StartCommitLog(RangeLogArgs(oldSha, newSha), repo.cwd);
    if (!lr.errorMessage.empty() || lr.count == 0) return 0;

    return ShowLogWindow(repo, L"", RangeLogArgs(oldSha, newSha),
                         std::move(lr));
}

std::wstring HeadSha(const std::wstring& cwd) {
    return TrimmedOutput(RunGit({L"rev-parse", L"HEAD"}, cwd));
}
}

int RunPullLog(int argc, wchar_t** argv) {
    if (IsDetachedInvocation(argc, argv) && argc >= 5) {
        return RunPullLogInChild(argv[3], argv[4]);
    }

    SetConsoleOutputCP(CP_UTF8);

    const std::wstring cwd = CurrentDirectory();

    const std::wstring oldSha = HeadSha(cwd);

    std::vector<std::wstring> args{L"pull"};
    for (auto& a : ArgsFrom(argc, argv, 2)) args.push_back(std::move(a));
    ProcessResult pull = RunGit(args, cwd, StdioMode::Inherit);
    if (!pull.started) return 1;
    if (pull.exitCode != 0) return pull.exitCode;

    const std::wstring newSha = HeadSha(cwd);

    if (oldSha.empty() || newSha.empty() || oldSha == newSha) return 0;

    return SpawnDetachedSelf(L"pull-log", {oldSha, newSha});
}
}
