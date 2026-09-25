#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace git_tools {

std::wstring ExecutablePath();

std::wstring CurrentDirectory();

bool SpawnDetachedProcess(const std::wstring& cwd,
                          const std::wstring& executable,
                          const std::vector<std::wstring>& args);

std::wstring LastSystemError();

bool ReadStandardInput(std::string& bytes);

std::wstring WriteTempFile(std::string_view bytes);

void RemoveFile(const std::wstring& path);

void UseUtf8Console();

void ReleaseConsole();

void WriteOut(const std::wstring& s);

void WriteErr(const std::wstring& s);

}
