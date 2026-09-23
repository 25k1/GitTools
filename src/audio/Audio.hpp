#pragma once

#include <string>
#include <vector>

namespace git_tools {

struct AudioDevice {
    std::wstring id;
    std::wstring name;
};

std::vector<AudioDevice> ListAudioDevices();

void PlaySoundResource(const wchar_t* name);

void CloseAudio();

}
