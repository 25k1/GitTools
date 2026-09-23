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

template <typename Child>
int RunDetached(const wchar_t* subcommand, int argc, wchar_t** argv,
                Child&& child) {
    if (IsDetachedInvocation(argc, argv)) return child(ArgsFrom(argc, argv, 3));
    return SpawnDetachedSelf(subcommand, ArgsFrom(argc, argv, 2));
}

}
