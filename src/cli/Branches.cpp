#include "cli/Branches.hpp"

#include "cli/Detached.hpp"
#include "git/Git.hpp"
#include "ui/App.hpp"
#include "ui/BranchWindow.hpp"

namespace git_tools {

namespace {

constexpr wchar_t kTitle[] = L"gittools branch";

int RunBranchInChild() {
    RepoContext repo;
    if (!OpenRepoOrReport(kTitle, repo)) return 1;

    BranchListResult lr = LoadBranchList(repo.cwd);
    if (!lr.errorMessage.empty()) {
        ShowError(nullptr, kTitle, lr.errorMessage);
        return 1;
    }
    if (lr.branches.empty()) {
        ShowError(nullptr, kTitle, L"No branches.");
        return 0;
    }
    return ShowBranchWindow({L"gittools - branches - " + repo.root, repo.cwd,
                             std::move(lr.branches)});
}

}

int RunBranch(int argc, wchar_t** argv) {
    return RunDetached(L"branch", argc, argv,
                       [](const std::vector<std::wstring>&) {
                           return RunGui(RunBranchInChild);
                       });
}

}
