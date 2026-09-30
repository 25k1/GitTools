#pragma once

#include "git/Git.hpp"

class wxWindow;

namespace git_tools {

bool ShowDateFilterDialog(wxWindow* owner, DateRange& range);

}
