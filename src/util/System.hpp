#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace git_tools {

inline constexpr wchar_t kNoExecutablePath[] =
    L"Failed to resolve the gittools executable path.";

std::wstring ExecutablePath();

std::wstring CurrentDirectory();

// Git runs `!` aliases from the work tree root and passes the caller's
// subdirectory in GIT_PREFIX; this moves back there. The variable is cleared
// so detached children and nested git calls don't apply it again.
void EnterGitPrefix();

bool SpawnDetachedProcess(const std::wstring& cwd,
                          const std::wstring& executable,
                          const std::vector<std::wstring>& args);

std::wstring LastSystemError();

bool ReadStandardInput(std::string& bytes);

std::string ReadFileBytes(const std::wstring& path);

std::wstring WriteTempFile(std::string_view bytes);

void RemoveFile(const std::wstring& path);

void UseUtf8Console();

void ReleaseConsole();

void WriteOut(const std::wstring& s);

void WriteErr(const std::wstring& s);

}
