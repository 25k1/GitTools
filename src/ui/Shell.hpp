#pragma once

#include <windows.h>

#include <string>

namespace git_tools {

std::wstring RepoFilePath(const std::wstring& repoRoot,
                          const std::wstring& relativePath);

std::wstring ParentDirectory(const std::wstring& path);

bool PathExists(const std::wstring& path);

std::string ReadFileBytes(const std::wstring& path);

std::wstring FindEditor();

void ResetEditorCache();

bool RevealInExplorer(HWND owner, const std::wstring& path);

bool OpenWithEditor(HWND owner, const std::wstring& editor,
                    const std::wstring& path, int line = 0);

}
