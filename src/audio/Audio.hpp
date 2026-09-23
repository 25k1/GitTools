#pragma once

#include <initializer_list>
#include <string>
#include <vector>

namespace git_tools {

struct AudioDevice {
    std::wstring id;
    std::wstring name;
};

std::vector<AudioDevice> ListAudioDevices();

void PrepareSounds(std::initializer_list<const wchar_t*> names);

void PlaySoundResource(const wchar_t* name);

void CloseAudio();

}
