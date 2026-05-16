#include "cli/Branches.hpp"

#include "cli/Detached.hpp"
#include "git/Git.hpp"
#include "ui/BranchWindow.hpp"

#include <windows.h>

namespace git_tools {

namespace {

constexpr wchar_t kTitle[] = L"gittools branch";

int RunBranchInChild() {
    RepoContext repo;
    if (!OpenRepoOrReport(kTitle, repo)) return 1;

    BranchListResult lr = LoadBranchList(repo.cwd);
    if (!lr.errorMessage.empty()) {
        ReportDialogError(kTitle, lr.errorMessage);
        return 1;
    }
    if (lr.branches.empty()) {
        ReportDialogError(kTitle, L"No branches.");
        return 0;
    }

    BranchWindowParams p;
    p.title    = L"gittools - branches - " + repo.root;
    p.cwd      = repo.cwd;
    p.branches = std::move(lr.branches);
    return ShowBranchWindow(p);
}

}

int RunBranch(int argc, wchar_t** argv) {
    if (IsDetachedInvocation(argc, argv)) return RunBranchInChild();
    return SpawnDetachedSelf(L"branch", ArgsFrom(argc, argv, 2));
}

}
