#pragma once

#include <algorithm>
#include <string>

namespace git_tools {

inline constexpr wchar_t kEditorKey[]           = L"editor";
inline constexpr wchar_t kSoundVolumeKey[]      = L"soundvolume";
inline constexpr wchar_t kWrapAroundKey[]       = L"wraparound";
inline constexpr wchar_t kDebugOutputKey[]      = L"debug";
inline constexpr wchar_t kUnloadFarCommitsKey[] = L"unloadfarcommits";
inline constexpr wchar_t kAudioDeviceKey[]      = L"audiodevice";
inline constexpr wchar_t kDiffMarkersKey[]      = L"diffmarkers";

std::wstring ConfigGet(const std::wstring& key,
                       const std::wstring& fallback = L"");

bool ConfigGetBool(const std::wstring& key, bool fallback);

int ConfigGetInt(const std::wstring& key, int fallback);

void ConfigSet(const std::wstring& key, const std::wstring& value);

void ConfigSetBool(const std::wstring& key, bool value);

bool DiffViewerInstalled();

bool SetDiffViewer(bool enabled);

inline int SoundVolumePercent() {
    return std::clamp(ConfigGetInt(kSoundVolumeKey, 50), 0, 100);
}

}
