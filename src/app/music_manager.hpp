#pragma once
#include "music_policy.hpp"
#include <memory>
#include <string>

namespace fasttris::app {

class MusicManager {
public:
    MusicManager();
    ~MusicManager();
    MusicManager(const MusicManager&) = delete;
    MusicManager& operator=(const MusicManager&) = delete;

    bool initialize();
    void shutdown();
    bool available() const;
    const std::string& lastError() const;

    void setVolume(int volume_percent);
    int volume() const;
    void setDesiredTrack(MusicTrack track);
    MusicTrack desiredTrack() const;
    MusicTrack currentTrack() const;
    void setPaused(bool paused);
    bool paused() const;
    void setLifecycleSuspended(bool suspended);
    void update();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fasttris::app
