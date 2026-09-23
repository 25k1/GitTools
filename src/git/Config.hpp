#pragma once

#include <string>

namespace git_tools {

inline constexpr wchar_t kUnloadFarCommitsKey[] = L"unloadfarcommits";

std::wstring ConfigGet(const std::wstring& key,
                       const std::wstring& fallback = L"");

bool ConfigGetBool(const std::wstring& key, bool fallback);

int ConfigGetInt(const std::wstring& key, int fallback);

void ConfigSet(const std::wstring& key, const std::wstring& value);

void ConfigSetBool(const std::wstring& key, bool value);

}
