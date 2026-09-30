#pragma once

#include "git/Process.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace git_tools {

inline constexpr wchar_t kEditorKey[]           = L"editor";
inline constexpr wchar_t kSoundVolumeKey[]      = L"soundvolume";
inline constexpr wchar_t kWrapAroundKey[]       = L"wraparound";
inline constexpr wchar_t kDebugOutputKey[]      = L"debug";
inline constexpr wchar_t kUnloadFarCommitsKey[] = L"unloadfarcommits";
inline constexpr wchar_t kAudioDeviceKey[]      = L"audiodevice";
inline constexpr wchar_t kDiffMarkersKey[]      = L"diffmarkers";
inline constexpr wchar_t kDebounceKey[]         = L"debounce";
inline constexpr wchar_t kLineWrapKey[]         = L"linewrap";

inline constexpr int kDefaultDebounceMs = 250;
inline constexpr int kMaxDebounceMs     = 10000;
inline constexpr int kMaxLineWrap       = 10000;

inline constexpr wchar_t kDiffViewCommand[] = L"diff-view";

std::wstring ConfigGet(const std::wstring& key,
                       const std::wstring& fallback = L"");

bool ConfigGetBool(const std::wstring& key, bool fallback);

int ConfigGetInt(const std::wstring& key, int fallback);

void ConfigSet(const std::wstring& key, const std::wstring& value);

void ConfigSetBool(const std::wstring& key, bool value);

void ConfigSetInt(const std::wstring& key, int value);

std::wstring GlobalConfigGet(const std::wstring& key);

ProcessResult GlobalConfigSet(const std::wstring& key, const std::wstring& value);

bool GlobalConfigUnset(const std::wstring& key);

std::wstring SelfCommand(std::wstring_view subcommand);

bool DiffViewerInstalled();

bool SetDiffViewer(bool enabled);

inline int ConfigGetClamped(const std::wstring& key, int fallback, int max) {
    return std::clamp(ConfigGetInt(key, fallback), 0, max);
}

inline int SoundVolumePercent() {
    return ConfigGetClamped(kSoundVolumeKey, 50, 100);
}

inline int DebounceMs() {
    return ConfigGetClamped(kDebounceKey, kDefaultDebounceMs, kMaxDebounceMs);
}

inline int LineWrapWidth() {
    return ConfigGetClamped(kLineWrapKey, 0, kMaxLineWrap);
}

}
