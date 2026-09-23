#pragma once

#include <string>
#include <vector>

namespace git_tools {

enum class LogErrors {
    Report,
    Ignore,
};

int OpenLogWindow(const wchar_t* title, std::wstring query,
                  std::vector<std::wstring> logArgs, LogErrors errors);

int RunLog(int argc, wchar_t** argv);

int RunLogRange(int argc, wchar_t** argv);

}
