#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#ifdef _WIN32
#define MA_ENABLE_WASAPI
#else
#define MA_ENABLE_PULSEAUDIO
#define MA_ENABLE_ALSA
#endif
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_MP3
#define MA_NO_WAV
#define MA_NO_SSE2
#define MA_NO_AVX2
#define MA_NO_NEON
#define MA_DR_FLAC_NO_SIMD
#define MA_DR_FLAC_NO_CRC
#define MA_DR_FLAC_NO_OGG
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "audio/Audio.hpp"

#include "git/Config.hpp"
#include "util/Encoding.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <cwchar>
#include <deque>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>

namespace git_tools {

namespace {

std::wstring DeviceId(const ma_context& context, const ma_device_id& id) {
#ifdef _WIN32
    (void)context;
    return std::wstring(id.wasapi, wcsnlen(id.wasapi, std::size(id.wasapi)));
#else
    const char* name = context.backend == ma_backend_pulseaudio ? id.pulse
                                                                : id.alsa;
    return Utf8ToWide(std::string_view(name, strnlen(name, sizeof(id.alsa))));
#endif
}

bool InitContext(ma_context& context) {
    return ma_context_init(nullptr, 0, nullptr, &context) == MA_SUCCESS;
}

template <typename F>
void ForEachPlaybackDevice(ma_context& context, F&& fn) {
    ma_device_info* infos = nullptr;
    ma_uint32       count = 0;
    if (ma_context_get_devices(&context, &infos, &count, nullptr, nullptr) !=
        MA_SUCCESS) {
        return;
    }
    for (ma_uint32 i = 0; i < count; ++i) fn(infos[i]);
}

std::vector<float> Decode(std::string_view flac, ma_uint32 channels,
                          ma_uint32 sampleRate) {
    unsigned int sourceChannels = 0;
    unsigned int sourceRate     = 0;
    ma_uint64    sourceFrames   = 0;
    float* source = flac.empty() || channels == 0
                        ? nullptr
                        : ma_dr_flac_open_memory_and_read_pcm_frames_f32(
                              flac.data(), flac.size(), &sourceChannels,
                              &sourceRate, &sourceFrames, nullptr);
    if (!source) return {};

    const auto convert = [&](float* out, ma_uint64 capacity) {
        return ma_convert_frames(out, capacity, ma_format_f32, channels,
                                 sampleRate, source, sourceFrames, ma_format_f32,
                                 sourceChannels, sourceRate);
    };
    std::vector<float> samples(convert(nullptr, 0) * channels);
    samples.resize(convert(samples.data(), samples.size() / channels) * channels);
    ma_dr_flac_free(source, nullptr);
    return samples;
}

class Player {
public:
    void Prepare(const std::wstring& device, const std::vector<Sound>& sounds) {
        if (!Open(device)) return;
        for (Sound sound : sounds) Clip(sound);
    }

    void Play(const std::wstring& device, Sound sound, float volume) {
        if (!Open(device)) return;
        const std::vector<float>& clip = Clip(sound);
        if (clip.empty()) return;

        ma_device_set_master_volume(&device_, volume);
        std::lock_guard lock(mu_);
        current_ = &clip;
        cursor_  = 0;
    }

    void Close() {
        if (open_) {
            ma_device_uninit(&device_);
            ma_context_uninit(&context_);
        }
        open_    = false;
        failed_  = false;
        current_ = nullptr;
        cursor_  = 0;
        clips_.clear();
    }

private:
    const std::vector<float>& Clip(Sound sound) {
        auto [clip, inserted] = clips_.try_emplace(sound);
        if (inserted) {
            clip->second = Decode(SoundBytes(sound),
                                  device_.playback.channels, device_.sampleRate);
        }
        return clip->second;
    }

    bool Open(const std::wstring& wanted) {
        if (open_ || failed_) return open_;
        failed_ = true;
        if (!InitContext(context_)) return false;

        ma_device_id id{};
        bool found = false;
        if (!wanted.empty()) {
            ForEachPlaybackDevice(context_, [&](const ma_device_info& info) {
                if (!found && DeviceId(context_, info.id) == wanted) {
                    id    = info.id;
                    found = true;
                }
            });
        }

        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.pDeviceID = found ? &id : nullptr;
        config.playback.format    = ma_format_f32;
        config.performanceProfile = ma_performance_profile_low_latency;
        config.dataCallback       = &Player::OnData;
        config.pUserData          = this;

        if (ma_device_init(&context_, &config, &device_) != MA_SUCCESS) {
            ma_context_uninit(&context_);
            return false;
        }
        if (ma_device_start(&device_) != MA_SUCCESS) {
            ma_device_uninit(&device_);
            ma_context_uninit(&context_);
            return false;
        }
        open_   = true;
        failed_ = false;
        return true;
    }

    static void OnData(ma_device* device, void* output, const void*,
                       ma_uint32 frames) {
        auto* self = static_cast<Player*>(device->pUserData);
        std::lock_guard lock(self->mu_);
        if (!self->current_) return;
        const std::vector<float>& clip = *self->current_;
        const size_t count =
            std::min(size_t{frames} * device->playback.channels,
                     clip.size() - self->cursor_);
        std::copy_n(clip.data() + self->cursor_, count,
                    static_cast<float*>(output));
        self->cursor_ += count;
        if (self->cursor_ >= clip.size()) self->current_ = nullptr;
    }

    ma_context                                 context_{};
    ma_device                                  device_{};
    bool                                       open_    = false;
    bool                                       failed_  = false;
    std::map<Sound, std::vector<float>>        clips_;
    std::mutex                                 mu_;
    const std::vector<float>*                  current_ = nullptr;
    size_t                                     cursor_  = 0;
};

class AudioThread {
public:
    template <typename F>
    void Post(F&& task) {
        {
            std::lock_guard lock(mu_);
            tasks_.emplace_back(std::forward<F>(task));
            if (!started_) {
                started_ = true;
                std::thread(&AudioThread::Run, this).detach();
            }
        }
        cv_.notify_one();
    }

    Player& player() { return player_; }

private:
    void Run() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lock(mu_);
                cv_.wait(lock, [this] { return !tasks_.empty(); });
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
        }
    }

    std::mutex                        mu_;
    std::condition_variable           cv_;
    std::deque<std::function<void()>> tasks_;
    bool                              started_ = false;
    Player                            player_;
};

AudioThread& Audio() {
    static AudioThread* audio = new AudioThread;
    return *audio;
}

}

std::vector<AudioDevice> ListAudioDevices() {
    ma_context context;
    if (!InitContext(context)) return {};
    std::vector<AudioDevice> devices;
    ForEachPlaybackDevice(context, [&](const ma_device_info& info) {
        devices.push_back({DeviceId(context, info.id), Utf8ToWide(info.name)});
    });
    ma_context_uninit(&context);
    return devices;
}

void PrepareSounds(std::initializer_list<Sound> sounds) {
    if (SoundVolumePercent() == 0) return;
    std::wstring device = ConfigGet(kAudioDeviceKey);
    std::vector<Sound> clips(sounds.begin(), sounds.end());
    Audio().Post([device = std::move(device), clips = std::move(clips)] {
        Audio().player().Prepare(device, clips);
    });
}

void PlaySoundEffect(Sound sound) {
    const int volume = SoundVolumePercent();
    if (volume <= 0) return;
    Audio().Post([device = ConfigGet(kAudioDeviceKey), sound,
                  gain = static_cast<float>(volume) / 100.0f] {
        Audio().player().Play(device, sound, gain);
    });
}

void CloseAudio() {
    Audio().Post([] { Audio().player().Close(); });
}

}
