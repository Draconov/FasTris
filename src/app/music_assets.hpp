#pragma once
#include "music_policy.hpp"
#include <cstddef>
#include <cstdint>

namespace fasttris::app {

enum class MusicFormat : int { Ogg = 0, Mp3, Wav };

struct EmbeddedMusicAsset {
    const std::uint8_t* data{};
    std::size_t size{};
    MusicFormat format{MusicFormat::Ogg};
    explicit operator bool() const { return data != nullptr && size > 0; }
};

EmbeddedMusicAsset embeddedMusicAsset(MusicTrack track);

} // namespace fasttris::app
