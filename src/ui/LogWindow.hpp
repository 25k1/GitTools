#pragma once

#include "git/Git.hpp"

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

namespace git_tools {

struct LogWindowParams {
    std::wstring                  query;
    std::wstring                  repoRoot;
    std::wstring                  workTree;
    std::wstring                  cwd;
    std::vector<std::wstring>     logArgs;
    std::unique_ptr<CommitLoader> loader;
};

int ShowLogWindow(LogWindowParams params);

int ShowLogWindow(const RepoContext& repo, std::wstring query,
                  std::vector<std::wstring> logArgs,
                  CommitListResult log);

}
