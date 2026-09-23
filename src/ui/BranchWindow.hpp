#pragma once

#include "git/Types.hpp"

#include <string>
#include <vector>

namespace git_tools {

struct BranchWindowParams {
    std::wstring        title;
    std::wstring        cwd;
    std::vector<Branch> branches;
};

int ShowBranchWindow(const BranchWindowParams& params);

}
