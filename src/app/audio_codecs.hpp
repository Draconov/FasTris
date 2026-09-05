#pragma once
#include "music_assets.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <string>

namespace fasttris::app {

class AudioDecoder {
public:
    virtual ~AudioDecoder() = default;
    virtual SDL_AudioSpec spec() const = 0;
    virtual int bytesPerFrame() const = 0;
    virtual int readFrames(void* destination, int max_frames) = 0;
    virtual bool rewind() = 0;
};

std::unique_ptr<AudioDecoder> createAudioDecoder(const EmbeddedMusicAsset& asset, std::string& error);

} // namespace fasttris::app
