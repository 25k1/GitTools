#pragma once

#include "git/Git.hpp"

#include <windows.h>

#include <string>
#include <vector>

namespace git_tools {

struct LogWindowParams {
    std::wstring              query;
    std::wstring              repoRoot;
    std::wstring              cwd;
    std::vector<std::wstring> logArgs;
    std::vector<Commit>       commits;
};

int ShowLogWindow(LogWindowParams params);

int ShowLogWindow(const RepoContext& repo, std::wstring query,
                  std::vector<std::wstring> logArgs,
                  std::vector<Commit> commits);

}
