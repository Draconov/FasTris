#include "audio_codecs.hpp"
#include <algorithm>
#include <climits>
#include <cstring>
#include <limits>
#include <memory>

#if defined(FASTTRIS_HAS_OGG)
#include "stb_vorbis.c"
#endif

#if defined(FASTTRIS_HAS_MP3)
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#endif

namespace fasttris::app {
namespace {

#if defined(FASTTRIS_HAS_OGG)
class VorbisDecoder final : public AudioDecoder {
public:
    explicit VorbisDecoder(const EmbeddedMusicAsset& asset, std::string& error) {
        if (asset.size > static_cast<std::size_t>(INT_MAX)) {
            error = "embedded OGG track is too large";
            return;
        }
        int decoder_error = 0;
        decoder_ = stb_vorbis_open_memory(asset.data, static_cast<int>(asset.size), &decoder_error, nullptr);
        if (!decoder_) {
            error = "stb_vorbis_open_memory failed (code " + std::to_string(decoder_error) + ")";
            return;
        }
        const auto info = stb_vorbis_get_info(decoder_);
        if (info.channels <= 0 || info.sample_rate == 0) {
            error = "OGG track has invalid audio metadata";
            stb_vorbis_close(decoder_);
            decoder_ = nullptr;
            return;
        }
        spec_.format = SDL_AUDIO_F32;
        spec_.channels = info.channels;
        spec_.freq = static_cast<int>(info.sample_rate);
    }

    ~VorbisDecoder() override {
        if (decoder_) stb_vorbis_close(decoder_);
    }

    bool valid() const { return decoder_ != nullptr; }
    SDL_AudioSpec spec() const override { return spec_; }
    int bytesPerFrame() const override { return static_cast<int>(sizeof(float)) * spec_.channels; }

    int readFrames(void* destination, int max_frames) override {
        if (!decoder_ || !destination || max_frames <= 0) return 0;
        const int floats = max_frames * spec_.channels;
        return stb_vorbis_get_samples_float_interleaved(
            decoder_, spec_.channels, static_cast<float*>(destination), floats);
    }

    bool rewind() override {
        return decoder_ && stb_vorbis_seek_start(decoder_) != 0;
    }

private:
    stb_vorbis* decoder_{};
    SDL_AudioSpec spec_{};
};
#endif

#if defined(FASTTRIS_HAS_MP3)
class Mp3Decoder final : public AudioDecoder {
public:
    explicit Mp3Decoder(const EmbeddedMusicAsset& asset, std::string& error) {
        if (!drmp3_init_memory(&decoder_, asset.data, asset.size, nullptr)) {
            error = "dr_mp3 could not decode embedded MP3 track";
            return;
        }
        initialized_ = true;
        if (decoder_.channels == 0 || decoder_.sampleRate == 0) {
            error = "MP3 track has invalid audio metadata";
            drmp3_uninit(&decoder_);
            initialized_ = false;
            return;
        }
        spec_.format = SDL_AUDIO_F32;
        spec_.channels = static_cast<int>(decoder_.channels);
        spec_.freq = static_cast<int>(decoder_.sampleRate);
    }

    ~Mp3Decoder() override {
        if (initialized_) drmp3_uninit(&decoder_);
    }

    bool valid() const { return initialized_; }
    SDL_AudioSpec spec() const override { return spec_; }
    int bytesPerFrame() const override { return static_cast<int>(sizeof(float)) * spec_.channels; }

    int readFrames(void* destination, int max_frames) override {
        if (!initialized_ || !destination || max_frames <= 0) return 0;
        const auto read = drmp3_read_pcm_frames_f32(
            &decoder_, static_cast<drmp3_uint64>(max_frames), static_cast<float*>(destination));
        return read > static_cast<drmp3_uint64>(INT_MAX) ? INT_MAX : static_cast<int>(read);
    }

    bool rewind() override {
        return initialized_ && drmp3_seek_to_pcm_frame(&decoder_, 0);
    }

private:
    drmp3 decoder_{};
    bool initialized_{};
    SDL_AudioSpec spec_{};
};
#endif

class WavDecoder final : public AudioDecoder {
public:
    explicit WavDecoder(const EmbeddedMusicAsset& asset, std::string& error) {
        SDL_IOStream* io = SDL_IOFromConstMem(asset.data, asset.size);
        if (!io) {
            error = std::string("SDL_IOFromConstMem failed: ") + SDL_GetError();
            return;
        }
        if (!SDL_LoadWAV_IO(io, true, &spec_, &data_, &length_)) {
            error = std::string("SDL_LoadWAV_IO failed: ") + SDL_GetError();
            return;
        }
        if (spec_.channels <= 0 || spec_.freq <= 0 || SDL_AUDIO_FRAMESIZE(spec_) <= 0) {
            error = "WAV track has invalid audio metadata";
            SDL_free(data_);
            data_ = nullptr;
            length_ = 0;
        }
    }

    ~WavDecoder() override { SDL_free(data_); }

    bool valid() const { return data_ != nullptr && length_ > 0; }
    SDL_AudioSpec spec() const override { return spec_; }
    int bytesPerFrame() const override { return SDL_AUDIO_FRAMESIZE(spec_); }

    int readFrames(void* destination, int max_frames) override {
        if (!valid() || !destination || max_frames <= 0) return 0;
        const auto frame_bytes = static_cast<std::size_t>(bytesPerFrame());
        const auto remaining = static_cast<std::size_t>(length_) - offset_;
        const auto available_frames = remaining / frame_bytes;
        const auto frames = std::min<std::size_t>(available_frames, static_cast<std::size_t>(max_frames));
        const auto bytes = frames * frame_bytes;
        if (bytes == 0) return 0;
        std::memcpy(destination, data_ + offset_, bytes);
        offset_ += bytes;
        return static_cast<int>(frames);
    }

    bool rewind() override {
        if (!valid()) return false;
        offset_ = 0;
        return true;
    }

private:
    SDL_AudioSpec spec_{};
    Uint8* data_{};
    Uint32 length_{};
    std::size_t offset_{};
};

} // namespace

std::unique_ptr<AudioDecoder> createAudioDecoder(const EmbeddedMusicAsset& asset, std::string& error) {
    error.clear();
    if (!asset) {
        error = "embedded music asset is empty";
        return {};
    }

    switch (asset.format) {
        case MusicFormat::Ogg:
#if defined(FASTTRIS_HAS_OGG)
        {
            auto decoder = std::make_unique<VorbisDecoder>(asset, error);
            if (decoder->valid()) return decoder;
            return {};
        }
#else
            error = "this build does not include OGG decoding support";
            return {};
#endif
        case MusicFormat::Mp3:
#if defined(FASTTRIS_HAS_MP3)
        {
            auto decoder = std::make_unique<Mp3Decoder>(asset, error);
            if (decoder->valid()) return decoder;
            return {};
        }
#else
            error = "this build does not include MP3 decoding support";
            return {};
#endif
        case MusicFormat::Wav: {
            auto decoder = std::make_unique<WavDecoder>(asset, error);
            if (decoder->valid()) return decoder;
            return {};
        }
    }
    error = "unknown embedded music format";
    return {};
}

} // namespace fasttris::app
