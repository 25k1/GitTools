#include "cli/Log.hpp"

#include "cli/Detached.hpp"
#include "git/Git.hpp"
#include "ui/DialogUtil.hpp"
#include "ui/LogWindow.hpp"

namespace git_tools {

int OpenLogWindow(const wchar_t* title, std::wstring query,
                  std::vector<std::wstring> logArgs, LogErrors errors) {
    RepoContext repo;
    if (!OpenRepoOrReport(title, repo)) return 1;

    CommitListResult lr = StartCommitLog(logArgs, repo.cwd);
    if (!lr.errorMessage.empty()) {
        if (errors == LogErrors::Ignore) return 0;
        ShowError(nullptr, title, lr.errorMessage);
        return 1;
    }
    if (lr.count == 0) return 0;
    return ShowLogWindow(repo, std::move(query), std::move(logArgs),
                         std::move(lr));
}

int RunLog(int argc, wchar_t** argv) {
    return RunDetached(L"log", argc, argv, [](std::vector<std::wstring> args) {
        std::wstring query = L"log";
        if (!args.empty()) query += L" " + Join(args, L" ");
        return OpenLogWindow(L"gittools log", std::move(query), std::move(args),
                             LogErrors::Report);
    });
}

int RunLogRange(int argc, wchar_t** argv) {
    constexpr wchar_t kTitle[] = L"gittools log-range";
    if (argc < 4) {
        ShowError(nullptr, kTitle, L"usage: gittools log-range OLD NEW");
        return 1;
    }
    return OpenLogWindow(kTitle, std::wstring(argv[2]) + L".." + argv[3],
                         RangeLogArgs(argv[2], argv[3]), LogErrors::Report);
}

}
