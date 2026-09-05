#include "music_manager.hpp"
#include "audio_codecs.hpp"
#include "music_assets.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fasttris::app {
namespace {
constexpr int kMixRate = 48'000;
constexpr int kMixChannels = 2;
constexpr int kMixFrameBytes = static_cast<int>(sizeof(float)) * kMixChannels;
constexpr int kDecodeFrames = 2'048;
constexpr int kMixFrames = 1'024;
constexpr int kQueueTargetFrames = 4'096; // ~250 ms; resilient without adding noticeable control latency.

SDL_AudioSpec mixSpec() {
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = kMixChannels;
    spec.freq = kMixRate;
    return spec;
}

class Voice {
public:
    Voice(MusicTrack track, std::string& error) : track_(track) {
        decoder_ = createAudioDecoder(embeddedMusicAsset(track), error);
        if (!decoder_) return;
        const auto source_spec = decoder_->spec();
        const auto output_spec = mixSpec();
        converter_ = SDL_CreateAudioStream(&source_spec, &output_spec);
        if (!converter_) {
            error = std::string("SDL_CreateAudioStream failed: ") + SDL_GetError();
            decoder_.reset();
            return;
        }
        const int frame_bytes = decoder_->bytesPerFrame();
        if (frame_bytes <= 0) {
            error = "music decoder reported an invalid frame size";
            SDL_DestroyAudioStream(converter_);
            converter_ = nullptr;
            decoder_.reset();
            return;
        }
        decode_buffer_.resize(static_cast<std::size_t>(frame_bytes) * kDecodeFrames);
    }

    ~Voice() {
        if (converter_) SDL_DestroyAudioStream(converter_);
    }

    bool valid() const { return decoder_ && converter_; }
    MusicTrack track() const { return track_; }

    bool render(float* destination, int frames, std::string& error) {
        if (!valid() || !destination || frames <= 0) return false;
        int produced = 0;
        while (produced < frames) {
            const int wanted_bytes = (frames - produced) * kMixFrameBytes;
            const int got = SDL_GetAudioStreamData(
                converter_, destination + static_cast<std::size_t>(produced) * kMixChannels, wanted_bytes);
            if (got < 0) {
                error = std::string("SDL_GetAudioStreamData failed: ") + SDL_GetError();
                return false;
            }
            if (got > 0) {
                if ((got % kMixFrameBytes) != 0) {
                    error = "SDL audio converter returned a partial sample frame";
                    return false;
                }
                produced += got / kMixFrameBytes;
                continue;
            }

            int decoded = decoder_->readFrames(decode_buffer_.data(), kDecodeFrames);
            if (decoded <= 0) {
                if (!decoder_->rewind()) {
                    error = "music decoder could not rewind loop";
                    return false;
                }
                decoded = decoder_->readFrames(decode_buffer_.data(), kDecodeFrames);
                if (decoded <= 0) {
                    error = "music track decoded no samples after rewind";
                    return false;
                }
            }
            const int decoded_bytes = decoded * decoder_->bytesPerFrame();
            if (!SDL_PutAudioStreamData(converter_, decode_buffer_.data(), decoded_bytes)) {
                error = std::string("SDL_PutAudioStreamData failed: ") + SDL_GetError();
                return false;
            }
        }
        return true;
    }

private:
    MusicTrack track_{MusicTrack::Menu};
    std::unique_ptr<AudioDecoder> decoder_;
    SDL_AudioStream* converter_{};
    std::vector<std::uint8_t> decode_buffer_;
};

} // namespace

struct MusicManager::Impl {
    SDL_AudioStream* output{};
    std::unique_ptr<Voice> current;
    std::unique_ptr<Voice> incoming;
    MusicTrack desired{MusicTrack::Menu};
    MusicTrack logical_current{MusicTrack::Menu};
    int volume_percent{70};
    bool paused{};
    bool lifecycle_suspended{};
    bool device_paused{true};
    std::uint64_t fade_frames_done{};
    std::string error;
    std::vector<float> a{static_cast<std::size_t>(kMixFrames) * kMixChannels};
    std::vector<float> b{static_cast<std::size_t>(kMixFrames) * kMixChannels};
    std::vector<float> mix{static_cast<std::size_t>(kMixFrames) * kMixChannels};

    static constexpr std::uint64_t fadeFramesTotal() {
        return static_cast<std::uint64_t>(kMusicCrossfadeSeconds * static_cast<float>(kMixRate) + 0.5f);
    }

    bool openVoice(MusicTrack track, std::unique_ptr<Voice>& into) {
        std::string voice_error;
        auto voice = std::make_unique<Voice>(track, voice_error);
        if (!voice->valid()) {
            error = "music track decode failed: " + voice_error;
            return false;
        }
        into = std::move(voice);
        return true;
    }

    void syncDevicePause() {
        if (!output) return;
        const bool should_pause = paused || lifecycle_suspended;
        if (should_pause == device_paused) return;
        const bool ok = should_pause ? SDL_PauseAudioStreamDevice(output)
                                     : SDL_ResumeAudioStreamDevice(output);
        if (!ok) {
            error = std::string(should_pause ? "could not pause music: " : "could not resume music: ") + SDL_GetError();
            return;
        }
        device_paused = should_pause;
    }

    bool renderChunk(int frames) {
        if (!current && !openVoice(desired, current)) return false;
        if (current && current->track() != desired) {
            if (!incoming || incoming->track() != desired) {
                incoming.reset();
                fade_frames_done = 0;
                if (!openVoice(desired, incoming)) return true; // Keep the working track playing.
            }
        } else if (incoming) {
            incoming.reset();
            fade_frames_done = 0;
        }

        if (!current->render(a.data(), frames, error)) {
            if (incoming && incoming->render(a.data(), frames, error)) {
                current = std::move(incoming);
                logical_current = current->track();
                fade_frames_done = 0;
            } else {
                return false;
            }
        }

        const float master = static_cast<float>(volume_percent) / 100.0f;
        if (!incoming) {
            for (int i = 0; i < frames * kMixChannels; ++i) mix[static_cast<std::size_t>(i)] = a[static_cast<std::size_t>(i)] * master;
        } else {
            if (!incoming->render(b.data(), frames, error)) {
                incoming.reset();
                fade_frames_done = 0;
                for (int i = 0; i < frames * kMixChannels; ++i) mix[static_cast<std::size_t>(i)] = a[static_cast<std::size_t>(i)] * master;
            } else {
                const auto total = fadeFramesTotal();
                for (int frame = 0; frame < frames; ++frame) {
                    const auto at = std::min<std::uint64_t>(fade_frames_done + static_cast<std::uint64_t>(frame), total);
                    const float t = total == 0 ? 1.0f : static_cast<float>(at) / static_cast<float>(total);
                    const auto gains = crossfadeGains(t);
                    for (int channel = 0; channel < kMixChannels; ++channel) {
                        const auto index = static_cast<std::size_t>(frame * kMixChannels + channel);
                        mix[index] = (a[index] * gains.from + b[index] * gains.to) * master;
                    }
                }
                fade_frames_done += static_cast<std::uint64_t>(frames);
                if (fade_frames_done >= total) {
                    current = std::move(incoming);
                    logical_current = current->track();
                    fade_frames_done = 0;
                }
            }
        }

        const int bytes = frames * kMixFrameBytes;
        if (!SDL_PutAudioStreamData(output, mix.data(), bytes)) {
            error = std::string("could not queue music: ") + SDL_GetError();
            return false;
        }
        return true;
    }
};

MusicManager::MusicManager() : impl_(std::make_unique<Impl>()) {}
MusicManager::~MusicManager() { shutdown(); }

bool MusicManager::initialize() {
    if (impl_->output) return true;
    impl_->error.clear();
    const auto spec = mixSpec();
    impl_->output = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!impl_->output) {
        impl_->error = std::string("SDL_OpenAudioDeviceStream failed: ") + SDL_GetError();
        return false;
    }
    impl_->device_paused = true;
    if (!impl_->openVoice(impl_->desired, impl_->current)) {
        SDL_DestroyAudioStream(impl_->output);
        impl_->output = nullptr;
        return false;
    }
    impl_->logical_current = impl_->desired;
    impl_->syncDevicePause();
    return true;
}

void MusicManager::shutdown() {
    if (!impl_) return;
    impl_->incoming.reset();
    impl_->current.reset();
    if (impl_->output) SDL_DestroyAudioStream(impl_->output);
    impl_->output = nullptr;
    impl_->device_paused = true;
    impl_->fade_frames_done = 0;
}

bool MusicManager::available() const { return impl_ && impl_->output != nullptr; }
const std::string& MusicManager::lastError() const { return impl_->error; }
void MusicManager::setVolume(int value) { impl_->volume_percent = std::clamp(value, 0, 100); }
int MusicManager::volume() const { return impl_->volume_percent; }
void MusicManager::setDesiredTrack(MusicTrack track) {
    if (track < MusicTrack::Menu || track >= MusicTrack::Count) track = MusicTrack::Menu;
    impl_->desired = track;
}
MusicTrack MusicManager::desiredTrack() const { return impl_->desired; }
MusicTrack MusicManager::currentTrack() const { return impl_->logical_current; }
void MusicManager::setPaused(bool value) { impl_->paused = value; impl_->syncDevicePause(); }
bool MusicManager::paused() const { return impl_->paused; }
void MusicManager::setLifecycleSuspended(bool value) { impl_->lifecycle_suspended = value; impl_->syncDevicePause(); }

void MusicManager::update() {
    if (!available()) return;
    impl_->syncDevicePause();
    if (impl_->paused || impl_->lifecycle_suspended) return;

    const int target_bytes = kQueueTargetFrames * kMixFrameBytes;
    int queued = SDL_GetAudioStreamQueued(impl_->output);
    if (queued < 0) {
        impl_->error = std::string("SDL_GetAudioStreamQueued failed: ") + SDL_GetError();
        return;
    }
    int guard = 0;
    while (queued < target_bytes && guard++ < 16) {
        if (!impl_->renderChunk(kMixFrames)) return;
        queued = SDL_GetAudioStreamQueued(impl_->output);
        if (queued < 0) {
            impl_->error = std::string("SDL_GetAudioStreamQueued failed: ") + SDL_GetError();
            return;
        }
    }
}

} // namespace fasttris::app
