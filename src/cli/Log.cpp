#include "cli/Log.hpp"

#include "cli/Detached.hpp"
#include "git/Git.hpp"
#include "ui/LogWindow.hpp"

#include <windows.h>

#include <string>
#include <vector>

namespace git_tools {

namespace {

constexpr wchar_t kTitle[] = L"gittools log";

std::wstring JoinArgs(const std::vector<std::wstring>& args) {
    std::wstring out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) out += L" ";
        out += args[i];
    }
    return out;
}

int RunLogInChild(const std::vector<std::wstring>& logArgs) {
    RepoContext repo;
    if (!OpenRepoOrReport(kTitle, repo)) return 1;

    CommitListResult lr = StartCommitLog(logArgs, repo.cwd);
    if (!lr.errorMessage.empty()) {
        ReportDialogError(kTitle, lr.errorMessage);
        return 1;
    }
    if (lr.count == 0) return 0;

    std::wstring query = L"log";
    if (!logArgs.empty()) query += L" " + JoinArgs(logArgs);

    return ShowLogWindow(repo, std::move(query), logArgs, std::move(lr));
}

}

int RunLog(int argc, wchar_t** argv) {
    if (IsDetachedInvocation(argc, argv)) {
        return RunLogInChild(ArgsFrom(argc, argv, 3));
    }
    return SpawnDetachedSelf(L"log", ArgsFrom(argc, argv, 2));
}

}
