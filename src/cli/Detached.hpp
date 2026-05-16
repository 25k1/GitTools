#pragma once

#include "git/Git.hpp"

#include <string>
#include <vector>

namespace git_tools {

inline constexpr wchar_t kDetachedFlag[] = L"--detached-internal";

bool IsDetachedInvocation(int argc, wchar_t** argv);

std::vector<std::wstring> ArgsFrom(int argc, wchar_t** argv, int first);

int SpawnDetachedSelf(const std::wstring& subcommand,
                      const std::vector<std::wstring>& args);

bool OpenRepoOrReport(const wchar_t* title, RepoContext& repo);

void ReportConsoleError(const std::wstring& msg);
void ReportDialogError(const std::wstring& title, const std::wstring& msg);

}
