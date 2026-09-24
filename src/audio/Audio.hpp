#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace git_tools {

enum class Sound {
    LineInserted,
    LineDeleted,
};

std::string_view SoundBytes(Sound sound);

struct AudioDevice {
    std::wstring id;
    std::wstring name;
};

std::vector<AudioDevice> ListAudioDevices();

void PrepareSounds(std::initializer_list<Sound> sounds);

void PlaySoundEffect(Sound sound);

void CloseAudio();

}
