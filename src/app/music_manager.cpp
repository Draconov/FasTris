#include "music_manager.hpp"
#include "audio_codecs.hpp"
#include "music_assets.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <mutex>
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
constexpr int kInitialPrebufferChunks = 2; // ~43 ms at 48 kHz; bounded startup work before device resume.

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
    std::array<std::unique_ptr<Voice>, static_cast<std::size_t>(MusicTrack::Count)> voices;
    MusicTrack current_track{MusicTrack::Menu}; // Audio callback thread after initialization.
    std::optional<MusicTrack> incoming_track; // Audio callback thread after initialization.
    std::atomic<int> desired{static_cast<int>(MusicTrack::Menu)};
    std::atomic<int> logical_current{static_cast<int>(MusicTrack::Menu)};
    std::atomic<int> volume_percent{70};
    std::atomic<bool> paused{false};
    std::atomic<bool> lifecycle_suspended{false};
    std::atomic<bool> fatal_error{false};
    bool device_paused{true}; // Main/control thread only after initialization completes.
    std::uint64_t fade_frames_done{}; // Audio callback thread only after resume.
    mutable std::mutex error_mutex;
    std::string error;
    std::vector<float> a{static_cast<std::size_t>(kMixFrames) * kMixChannels};
    std::vector<float> b{static_cast<std::size_t>(kMixFrames) * kMixChannels};
    std::vector<float> mix{static_cast<std::size_t>(kMixFrames) * kMixChannels};

    static constexpr std::uint64_t fadeFramesTotal() {
        return static_cast<std::uint64_t>(kMusicCrossfadeSeconds * static_cast<float>(kMixRate) + 0.5f);
    }

    MusicTrack desiredTrack() const {
        const int value = desired.load(std::memory_order_relaxed);
        if (value < static_cast<int>(MusicTrack::Menu) || value >= static_cast<int>(MusicTrack::Count))
            return MusicTrack::Menu;
        return static_cast<MusicTrack>(value);
    }

    void setError(std::string value) {
        std::lock_guard<std::mutex> lock(error_mutex);
        error = std::move(value);
    }

    void clearError() {
        std::lock_guard<std::mutex> lock(error_mutex);
        error.clear();
    }

    std::string errorCopy() const {
        std::lock_guard<std::mutex> lock(error_mutex);
        return error;
    }

    bool openVoice(MusicTrack track, std::unique_ptr<Voice>& into) {
        std::string voice_error;
        auto voice = std::make_unique<Voice>(track, voice_error);
        if (!voice->valid()) {
            setError("music track decode failed: " + voice_error);
            return false;
        }
        into = std::move(voice);
        return true;
    }

    Voice* voiceFor(MusicTrack track) const {
        const auto index = static_cast<std::size_t>(track);
        if (index >= voices.size()) return nullptr;
        return voices[index].get();
    }

    bool prepareAllVoices() {
        for (std::size_t i = 0; i < voices.size(); ++i) {
            const auto track = static_cast<MusicTrack>(i);
            if (!openVoice(track, voices[i])) {
                for (auto& voice : voices) voice.reset();
                return false;
            }
        }
        return true;
    }

    void syncDevicePause() {
        if (!output) return;
        const bool should_pause = paused.load(std::memory_order_relaxed) ||
                                  lifecycle_suspended.load(std::memory_order_relaxed);
        if (should_pause == device_paused) return;
        const bool ok = should_pause ? SDL_PauseAudioStreamDevice(output)
                                     : SDL_ResumeAudioStreamDevice(output);
        if (!ok) {
            setError(std::string(should_pause ? "could not pause music: " : "could not resume music: ") + SDL_GetError());
            fatal_error.store(true, std::memory_order_release);
            return;
        }
        device_paused = should_pause;
    }

    bool renderChunk(int frames) {
        const MusicTrack wanted = desiredTrack();
        Voice* current = voiceFor(current_track);
        if (!current) {
            setError("prepared current music voice is unavailable");
            return false;
        }

        if (current_track != wanted) {
            if (!incoming_track || *incoming_track != wanted) {
                if (!voiceFor(wanted)) {
                    setError("prepared requested music voice is unavailable");
                    return true; // Keep the working track playing.
                }
                incoming_track = wanted;
                fade_frames_done = 0;
            }
        } else if (incoming_track) {
            incoming_track.reset();
            fade_frames_done = 0;
        }

        std::string render_error;
        if (!current->render(a.data(), frames, render_error)) {
            setError(render_error);
            render_error.clear();
            Voice* incoming = incoming_track ? voiceFor(*incoming_track) : nullptr;
            if (incoming && incoming->render(a.data(), frames, render_error)) {
                current_track = *incoming_track;
                current = incoming;
                logical_current.store(static_cast<int>(current_track), std::memory_order_relaxed);
                incoming_track.reset();
                fade_frames_done = 0;
            } else {
                if (!render_error.empty()) setError(render_error);
                return false;
            }
        }

        const float master = static_cast<float>(volume_percent.load(std::memory_order_relaxed)) / 100.0f;
        if (!incoming_track) {
            for (int i = 0; i < frames * kMixChannels; ++i)
                mix[static_cast<std::size_t>(i)] = a[static_cast<std::size_t>(i)] * master;
        } else {
            Voice* incoming = voiceFor(*incoming_track);
            render_error.clear();
            if (!incoming || !incoming->render(b.data(), frames, render_error)) {
                if (!render_error.empty()) setError(render_error);
                incoming_track.reset();
                fade_frames_done = 0;
                for (int i = 0; i < frames * kMixChannels; ++i)
                    mix[static_cast<std::size_t>(i)] = a[static_cast<std::size_t>(i)] * master;
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
                    current_track = *incoming_track;
                    logical_current.store(static_cast<int>(current_track), std::memory_order_relaxed);
                    incoming_track.reset();
                    fade_frames_done = 0;
                }
            }
        }

        const int bytes = frames * kMixFrameBytes;
        if (!SDL_PutAudioStreamData(output, mix.data(), bytes)) {
            setError(std::string("could not queue music: ") + SDL_GetError());
            return false;
        }
        return true;
    }

    static void SDLCALL musicAudioCallback(void* userdata, SDL_AudioStream*, int additional_amount, int) {
        auto* self = static_cast<Impl*>(userdata);
        if (!self || additional_amount <= 0) return;
        if (self->paused.load(std::memory_order_relaxed) ||
            self->lifecycle_suspended.load(std::memory_order_relaxed)) return;

        int frames_remaining = (additional_amount + kMixFrameBytes - 1) / kMixFrameBytes;
        while (frames_remaining > 0) {
            const int frames = std::min(frames_remaining, kMixFrames);
            if (!self->renderChunk(frames)) {
                // Never let a broken audio backend spin forever. The SDL app
                // loop will observe this latch and tear down only music.
                self->fatal_error.store(true, std::memory_order_release);
                return;
            }
            frames_remaining -= frames;
        }
    }

    bool boundedInitialPrebuffer() {
        for (int i = 0; i < kInitialPrebufferChunks; ++i) {
            if (!renderChunk(kMixFrames)) return false;
        }
        return true;
    }
};
MusicManager::MusicManager() : impl_(std::make_unique<Impl>()) {}
MusicManager::~MusicManager() { shutdown(); }

bool MusicManager::initialize() {
    if (impl_->output) return true;
    impl_->fatal_error.store(false, std::memory_order_relaxed);
    impl_->clearError();

    // Decoder construction and SDL_AudioStream conversion setup may allocate or
    // perform substantial codec work. Prepare every possible music voice on the
    // control/startup thread before SDL can ever invoke the realtime callback.
    if (!impl_->prepareAllVoices()) return false;

    // Select the initial prepared voice before opening the device. Once the
    // SDL stream exists, current_track/incoming_track belong to the callback
    // thread until the stream is destroyed.
    const MusicTrack wanted = impl_->desiredTrack();
    impl_->current_track = wanted;
    impl_->incoming_track.reset();
    impl_->logical_current.store(static_cast<int>(wanted), std::memory_order_relaxed);

    const auto spec = mixSpec();
    impl_->output = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Impl::musicAudioCallback, impl_.get());
    if (!impl_->output) {
        impl_->setError(std::string("SDL_OpenAudioDeviceStream failed: ") + SDL_GetError());
        for (auto& voice : impl_->voices) voice.reset();
        return false;
    }
    impl_->device_paused = true;

    // Keep startup bounded: prebuffer only two small chunks, then let SDL ask
    // for exactly the amount it needs through musicAudioCallback. Decoding and
    // queue refills never run from SDL_AppIterate, so input delivery cannot be
    // starved by audio work.
    if (!impl_->boundedInitialPrebuffer()) {
        for (auto& voice : impl_->voices) voice.reset();
        SDL_DestroyAudioStream(impl_->output);
        impl_->output = nullptr;
        return false;
    }
    impl_->syncDevicePause();
    return true;
}

void MusicManager::shutdown() {
    if (!impl_) return;
    if (impl_->output && !impl_->device_paused) {
        SDL_PauseAudioStreamDevice(impl_->output);
        impl_->device_paused = true;
    }
    if (impl_->output) SDL_DestroyAudioStream(impl_->output);
    impl_->output = nullptr;
    impl_->incoming_track.reset();
    for (auto& voice : impl_->voices) voice.reset();
    impl_->fade_frames_done = 0;
    impl_->fatal_error.store(false, std::memory_order_relaxed);
}

bool MusicManager::available() const {
    return impl_ && impl_->output != nullptr && !impl_->fatal_error.load(std::memory_order_acquire);
}
std::string MusicManager::lastError() const { return impl_ ? impl_->errorCopy() : std::string{}; }
void MusicManager::setVolume(int value) { impl_->volume_percent.store(std::clamp(value, 0, 100), std::memory_order_relaxed); }
int MusicManager::volume() const { return impl_->volume_percent.load(std::memory_order_relaxed); }
void MusicManager::setDesiredTrack(MusicTrack track) {
    if (track < MusicTrack::Menu || track >= MusicTrack::Count) track = MusicTrack::Menu;
    impl_->desired.store(static_cast<int>(track), std::memory_order_relaxed);
}
MusicTrack MusicManager::desiredTrack() const { return impl_->desiredTrack(); }
MusicTrack MusicManager::currentTrack() const {
    const int value = impl_->logical_current.load(std::memory_order_relaxed);
    if (value < static_cast<int>(MusicTrack::Menu) || value >= static_cast<int>(MusicTrack::Count))
        return MusicTrack::Menu;
    return static_cast<MusicTrack>(value);
}
void MusicManager::setPaused(bool value) {
    impl_->paused.store(value, std::memory_order_relaxed);
    impl_->syncDevicePause();
}
bool MusicManager::paused() const { return impl_->paused.load(std::memory_order_relaxed); }
void MusicManager::setLifecycleSuspended(bool value) {
    impl_->lifecycle_suspended.store(value, std::memory_order_relaxed);
    impl_->syncDevicePause();
}

void MusicManager::update() {
    // Intentionally tiny: all decode/refill work is demand-driven by SDL's
    // audio callback. The application/event loop must never pump music data.
    // If the callback/device reports a fatal error, destroy only the audio
    // stream here on the control thread and leave gameplay/input untouched.
    if (impl_->fatal_error.load(std::memory_order_acquire)) {
        if (impl_->output) {
            SDL_PauseAudioStreamDevice(impl_->output);
            SDL_DestroyAudioStream(impl_->output);
            impl_->output = nullptr;
        }
        impl_->incoming_track.reset();
        for (auto& voice : impl_->voices) voice.reset();
        impl_->fade_frames_done = 0;
        impl_->device_paused = true;
        return;
    }
    impl_->syncDevicePause();
}

} // namespace fasttris::app
