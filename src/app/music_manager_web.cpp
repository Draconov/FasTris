#include "music_manager.hpp"
#include "music_assets.hpp"
#include <emscripten/emscripten.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

EM_JS(int, fasttrisWebMusicInitialize,
      (const std::uint8_t* menu_ptr, int menu_size,
       const std::uint8_t* gameplay_ptr, int gameplay_size,
       const std::uint8_t* intense_ptr, int intense_size,
       double fade_seconds, int initial_volume, int initial_track), {
    try {
        if (Module.fasttrisMusic && Module.fasttrisMusic.initialized) {
            return Module.fasttrisMusic.failed ? 0 : 1;
        }

        var AudioContextCtor = window.AudioContext || window.webkitAudioContext;
        if (!AudioContextCtor) {
            console.warn('FasTris music disabled: Web Audio API is unavailable');
            Module.fasttrisMusic = { initialized: true, failed: true, dead: false };
            return 0;
        }

        var ctx = new AudioContextCtor({ latencyHint: 'playback' });
        var master = ctx.createGain();
        master.gain.value = Math.max(0, Math.min(1, initial_volume / 100));
        master.connect(ctx.destination);

        var state = {
            initialized: true,
            failed: false,
            dead: false,
            ctx: ctx,
            master: master,
            buffers: [null, null, null],
            failedTracks: [false, false, false],
            desired: Math.max(0, Math.min(2, initial_track | 0)),
            current: -1,
            currentSource: null,
            currentGain: null,
            retiringSources: [],
            paused: false,
            lifecycleSuspended: false,
            unlocked: false,
            fadeSeconds: Math.max(0.05, +fade_seconds || 1.75),
            unlockHandler: null
        };
        Module.fasttrisMusic = state;

        state.warn = function(message, error) {
            var detail = '';
            try { detail = error ? (error.message || String(error)) : ''; } catch (_) {}
            console.warn('FasTris music disabled/limited: ' + message + (detail ? ': ' + detail : ''));
        };

        var curvePoints = 64;
        state.fadeInCurve = new Float32Array(curvePoints);
        state.fadeOutCurve = new Float32Array(curvePoints);
        for (var i = 0; i < curvePoints; ++i) {
            var t = i / (curvePoints - 1);
            state.fadeInCurve[i] = Math.sin(t * Math.PI * 0.5);
            state.fadeOutCurve[i] = Math.cos(t * Math.PI * 0.5);
        }

        state.stopSource = function(source) {
            if (!source) return;
            try { source.stop(); } catch (_) {}
            try { source.disconnect(); } catch (_) {}
        };

        state.applyDesired = function() {
            if (state.dead || state.failed || !state.unlocked || state.paused || state.lifecycleSuspended) return;
            var track = state.desired | 0;
            var buffer = state.buffers[track];
            if (!buffer || state.current === track) return;

            try {
                var now = state.ctx.currentTime;
                var source = state.ctx.createBufferSource();
                var gain = state.ctx.createGain();
                source.buffer = buffer;
                source.loop = true;
                source.connect(gain);
                gain.connect(state.master);

                var oldSource = state.currentSource;
                var oldGain = state.currentGain;
                if (oldSource && oldGain) {
                    gain.gain.setValueAtTime(0, now);
                    gain.gain.setValueCurveAtTime(state.fadeInCurve, now, state.fadeSeconds);
                    try {
                        oldGain.gain.cancelScheduledValues(now);
                        oldGain.gain.setValueAtTime(1, now);
                        oldGain.gain.setValueCurveAtTime(state.fadeOutCurve, now, state.fadeSeconds);
                    } catch (error) {
                        state.warn('could not schedule outgoing crossfade', error);
                    }
                    state.retiringSources.push(oldSource);
                    setTimeout(function() {
                        state.stopSource(oldSource);
                        var index = state.retiringSources.indexOf(oldSource);
                        if (index >= 0) state.retiringSources.splice(index, 1);
                    }, Math.ceil((state.fadeSeconds + 0.10) * 1000));
                } else {
                    gain.gain.setValueAtTime(1, now);
                }

                source.start(now);
                state.currentSource = source;
                state.currentGain = gain;
                state.current = track;
            } catch (error) {
                state.warn('could not start requested track', error);
            }
        };

        state.syncSuspended = function() {
            if (state.dead || state.failed) return;
            var shouldSuspend = state.paused || state.lifecycleSuspended;
            try {
                if (shouldSuspend) {
                    var suspended = state.ctx.suspend();
                    if (suspended && suspended.catch) suspended.catch(function(error) {
                        state.warn('could not suspend Web Audio', error);
                    });
                } else if (state.unlocked) {
                    var resumed = state.ctx.resume();
                    if (resumed && resumed.then) {
                        resumed.then(function() { state.applyDesired(); }).catch(function(error) {
                            state.warn('could not resume Web Audio', error);
                        });
                    } else {
                        state.applyDesired();
                    }
                }
            } catch (error) {
                state.warn('Web Audio suspend/resume failed', error);
            }
        };

        state.unlockHandler = function() {
            if (state.dead || state.failed) return;
            if (state.unlocked && state.ctx.state === 'running') return;
            state.unlocked = true;
            state.syncSuspended();
        };
        // These listeners never cancel the event. SDL receives the exact same
        // keyboard/pointer/touch input regardless of whether music unlocks.
        window.addEventListener('keydown', state.unlockHandler, { passive: true });
        window.addEventListener('pointerdown', state.unlockHandler, { passive: true });
        window.addEventListener('touchstart', state.unlockHandler, { passive: true });

        state.decodeTrack = function(index, ptr, size) {
            var schedule = window.requestIdleCallback
                ? function(cb) { window.requestIdleCallback(cb, { timeout: 1000 }); }
                : function(cb) { setTimeout(cb, 0); };
            schedule(function() {
                if (state.dead || state.failed) return;
                try {
                    // Copy only when the browser is idle; embedded C++ bytes are
                    // static, so no synchronous copying/decoding occurs in the
                    // SDL input handler.
                    var start = ptr >>> 0;
                    var length = size >>> 0;
                    var bytes = HEAPU8.slice(start, start + length);
                    state.ctx.decodeAudioData(
                        bytes.buffer,
                        function(buffer) {
                            if (state.dead) return;
                            state.buffers[index] = buffer;
                            state.applyDesired();
                        },
                        function(error) {
                            if (state.dead) return;
                            state.failedTracks[index] = true;
                            state.warn('track decode failed (' + index + ')', error);
                            if (state.failedTracks[0] && state.failedTracks[1] && state.failedTracks[2]) {
                                state.failed = true;
                                try { state.ctx.suspend(); } catch (_) {}
                            }
                        }
                    );
                } catch (error) {
                    state.failedTracks[index] = true;
                    state.warn('track decode setup failed (' + index + ')', error);
                    if (state.failedTracks[0] && state.failedTracks[1] && state.failedTracks[2]) {
                        state.failed = true;
                        try { state.ctx.suspend(); } catch (_) {}
                    }
                }
            });
        };

        state.decodeTrack(0, menu_ptr, menu_size);
        state.decodeTrack(1, gameplay_ptr, gameplay_size);
        state.decodeTrack(2, intense_ptr, intense_size);
        return 1;
    } catch (error) {
        try { console.warn('FasTris music disabled: Web Audio initialization failed', error); } catch (_) {}
        Module.fasttrisMusic = { initialized: true, failed: true, dead: false };
        return 0;
    }
});

EM_JS(void, fasttrisWebMusicSetVolume, (int volume_percent), {
    var state = Module.fasttrisMusic;
    if (!state || state.dead || state.failed || !state.master) return;
    try {
        var value = Math.max(0, Math.min(1, volume_percent / 100));
        state.master.gain.setTargetAtTime(value, state.ctx.currentTime, 0.015);
    } catch (error) {
        state.warn('volume update failed', error);
    }
});

EM_JS(void, fasttrisWebMusicSetTrack, (int track), {
    var state = Module.fasttrisMusic;
    if (!state || state.dead || state.failed) return;
    state.desired = Math.max(0, Math.min(2, track | 0));
    state.applyDesired();
});

EM_JS(void, fasttrisWebMusicSetPaused, (int paused, int lifecycle_suspended), {
    var state = Module.fasttrisMusic;
    if (!state || state.dead || state.failed) return;
    state.paused = !!paused;
    state.lifecycleSuspended = !!lifecycle_suspended;
    state.syncSuspended();
});

EM_JS(void, fasttrisWebMusicShutdown, (), {
    var state = Module.fasttrisMusic;
    if (!state || state.dead) return;
    state.dead = true;
    try { window.removeEventListener('keydown', state.unlockHandler); } catch (_) {}
    try { window.removeEventListener('pointerdown', state.unlockHandler); } catch (_) {}
    try { window.removeEventListener('touchstart', state.unlockHandler); } catch (_) {}
    try { state.stopSource(state.currentSource); } catch (_) {}
    if (state.retiringSources) {
        for (var i = 0; i < state.retiringSources.length; ++i) {
            try { state.stopSource(state.retiringSources[i]); } catch (_) {}
        }
    }
    try { if (state.master) state.master.disconnect(); } catch (_) {}
    try {
        var closed = state.ctx && state.ctx.close ? state.ctx.close() : null;
        if (closed && closed.catch) closed.catch(function() {});
    } catch (_) {}
    Module.fasttrisMusic = null;
});

namespace fasttris::app {

struct MusicManager::Impl {
    bool initialized{};
    bool failed{};
    int volume_percent{70};
    MusicTrack desired{MusicTrack::Menu};
    MusicTrack logical_current{MusicTrack::Menu};
    bool paused{};
    bool lifecycle_suspended{};
    std::string error;
};

MusicManager::MusicManager() : impl_(std::make_unique<Impl>()) {}
MusicManager::~MusicManager() { shutdown(); }

bool MusicManager::initialize() {
    if (impl_->initialized) return !impl_->failed;
    impl_->initialized = true;

    const auto menu = embeddedMusicAsset(MusicTrack::Menu);
    const auto gameplay = embeddedMusicAsset(MusicTrack::Gameplay);
    const auto intense = embeddedMusicAsset(MusicTrack::Intense);
    if (!menu || !gameplay || !intense) {
        impl_->failed = true;
        impl_->error = "embedded Web music asset is missing; continuing silently";
        return false;
    }

    const bool ok = fasttrisWebMusicInitialize(
        menu.data, static_cast<int>(menu.size),
        gameplay.data, static_cast<int>(gameplay.size),
        intense.data, static_cast<int>(intense.size),
        static_cast<double>(kMusicCrossfadeSeconds), impl_->volume_percent,
        static_cast<int>(impl_->desired)) != 0;
    if (!ok) {
        impl_->failed = true;
        impl_->error = "Web Audio backend unavailable; continuing silently";
        return false;
    }
    return true;
}

void MusicManager::shutdown() {
    if (!impl_ || !impl_->initialized) return;
    fasttrisWebMusicShutdown();
    impl_->initialized = false;
}

bool MusicManager::available() const { return impl_ && impl_->initialized && !impl_->failed; }
std::string MusicManager::lastError() const { return impl_ ? impl_->error : std::string{}; }

void MusicManager::setVolume(int value) {
    if (!impl_) return;
    value = std::clamp(value, 0, 100);
    if (value == impl_->volume_percent) return;
    impl_->volume_percent = value;
    if (available()) fasttrisWebMusicSetVolume(value);
}

int MusicManager::volume() const { return impl_ ? impl_->volume_percent : 0; }

void MusicManager::setDesiredTrack(MusicTrack track) {
    if (!impl_) return;
    if (track < MusicTrack::Menu || track >= MusicTrack::Count) track = MusicTrack::Menu;
    if (track == impl_->desired) return;
    impl_->desired = track;
    impl_->logical_current = track;
    if (available()) fasttrisWebMusicSetTrack(static_cast<int>(track));
}

MusicTrack MusicManager::desiredTrack() const { return impl_ ? impl_->desired : MusicTrack::Menu; }
MusicTrack MusicManager::currentTrack() const { return impl_ ? impl_->logical_current : MusicTrack::Menu; }

void MusicManager::setPaused(bool value) {
    if (!impl_ || impl_->paused == value) return;
    impl_->paused = value;
    if (available()) fasttrisWebMusicSetPaused(value ? 1 : 0, impl_->lifecycle_suspended ? 1 : 0);
}

bool MusicManager::paused() const { return impl_ && impl_->paused; }

void MusicManager::setLifecycleSuspended(bool value) {
    if (!impl_ || impl_->lifecycle_suspended == value) return;
    impl_->lifecycle_suspended = value;
    if (available()) fasttrisWebMusicSetPaused(impl_->paused ? 1 : 0, value ? 1 : 0);
}

void MusicManager::update() {
    // Web Audio owns decoding, looping and crossfades. The SDL application
    // loop deliberately performs no audio work so input/rendering stay
    // independent even if browser audio is unsupported or fails later.
}

} // namespace fasttris::app
