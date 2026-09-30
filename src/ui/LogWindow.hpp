#pragma once

#include "git/Git.hpp"

#include <string>
#include <vector>

namespace git_tools {

int ShowLogWindow(const RepoContext& repo, std::wstring query,
                  std::vector<std::wstring> logArgs,
                  CommitListResult log);

}
