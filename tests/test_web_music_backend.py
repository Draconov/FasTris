#!/usr/bin/env python3
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
main = (ROOT / 'src/app/main.cpp').read_text(encoding='utf-8')
web_backend = ROOT / 'src/app/music_manager_web.cpp'
native_backend = (ROOT / 'src/app/music_manager.cpp').read_text(encoding='utf-8')

assert web_backend.is_file(), 'dedicated Emscripten Web Audio backend is missing'
web = web_backend.read_text(encoding='utf-8')

for marker in ['AudioContext', 'decodeAudioData', 'setValueCurveAtTime', 'ctx.suspend()', 'ctx.resume()']:
    assert marker in web, f'web music backend missing required marker: {marker}'
for forbidden in ['SDL_OpenAudioDeviceStream', 'SDL_InitSubSystem', 'SDL_PutAudioStreamData']:
    assert forbidden not in web, f'web music backend must not use SDL audio: {forbidden}'

assert 'music_manager_web.cpp' in cmake, 'CMake does not select the Web Audio backend'
assert re.search(r'if\(EMSCRIPTEN\).*?music_manager_web\.cpp', cmake, re.S), 'Web backend must be selected in the EMSCRIPTEN branch'
assert re.search(r'else\(\).*?music_manager\.cpp.*?audio_codecs\.cpp', cmake, re.S), 'native backend must retain SDL music + codecs'

# Native codec dependency downloads are unnecessary on Web because decodeAudioData handles OGG/MP3/WAV.
assert 'if(NOT EMSCRIPTEN AND ".ogg" IN_LIST FASTTRIS_MUSIC_EXTENSIONS)' in cmake, 'fasttris_stb must be gated off for Emscripten'
assert 'if(NOT EMSCRIPTEN AND ".mp3" IN_LIST FASTTRIS_MUSIC_EXTENSIONS)' in cmake, 'fasttris_dr_libs must be gated off for Emscripten'

# The SDL event handler must never synchronously initialize music from keyboard/touch/gamepad input.
on_event_start = main.index('SDL_AppResult onEvent(SDL_Event& ev)')
on_event_end = main.index('SDL_AppResult iterate()', on_event_start)
on_event = main[on_event_start:on_event_end]
assert 'ensureMusicInitialized(true)' not in on_event, 'Web input path still synchronously initializes music'

# Web backend setup is independent of SDL audio initialization and must be attempted before user input.
assert '#if defined(__EMSCRIPTEN__)\n        ensureMusicInitialized(false);' in main, 'Web backend must initialize independently of input events'

# Browser gesture listener must be passive and must never cancel SDL input.
assert 'passive: true' in web, 'Web audio unlock listeners must be passive'
for forbidden in ['preventDefault(', 'stopPropagation(', 'stopImmediatePropagation(']:
    assert forbidden not in web, f'Web music unlock must not consume browser input: {forbidden}'


# Native mode switches must not construct decoders/converters on SDL's realtime callback.
render_start = native_backend.index('bool renderChunk(int frames)')
callback_start = native_backend.index('static void SDLCALL musicAudioCallback', render_start)
render_chunk = native_backend[render_start:callback_start]
for forbidden in ['openVoice(', 'std::make_unique<Voice>', 'SDL_CreateAudioStream(', 'createAudioDecoder(']:
    assert forbidden not in render_chunk, f'native realtime render path still prepares music resources: {forbidden}'

callback_end = native_backend.index('bool boundedInitialPrebuffer()', callback_start)
audio_callback = native_backend[callback_start:callback_end]
for forbidden in ['openVoice(', 'std::make_unique<Voice>', 'SDL_CreateAudioStream(', 'createAudioDecoder(']:
    assert forbidden not in audio_callback, f'native SDL callback still prepares music resources: {forbidden}'

# Every track must be prepared before the audio device is resumed so switching modes
# cannot trigger decoder/converter construction.
initialize_start = native_backend.index('bool MusicManager::initialize()')
shutdown_start = native_backend.index('void MusicManager::shutdown()', initialize_start)
initialize_body = native_backend[initialize_start:shutdown_start]
assert 'prepareAllVoices()' in initialize_body, 'native backend does not pre-prepare all music voices'
assert initialize_body.index('prepareAllVoices()') < initialize_body.index('SDL_OpenAudioDeviceStream('), \
    'music voices must be prepared before opening the audio device'
assert initialize_body.index('current_track = wanted') < initialize_body.index('SDL_OpenAudioDeviceStream('), \
    'initial prepared track must be selected before the callback can start'

# Native runtime audio failures must latch and tear down only the music stream.
for marker in ['std::atomic<bool> fatal_error', 'fatal_error.store(true', 'SDL_DestroyAudioStream(impl_->output)', 'leave gameplay/input untouched']:
    assert marker in native_backend, f'native fail-open music marker missing: {marker}'

# Shipped Vorbis assets use a conservative profile. This keeps embedded size and
# decoder pressure bounded across native stb_vorbis and browser Web Audio paths.
def vorbis_identification(path: Path):
    data = path.read_bytes()[:65536]
    marker = data.find(b"\x01vorbis")
    assert marker >= 0, f'{path.name} is missing a Vorbis identification header'
    header = data[marker:marker + 30]
    assert len(header) >= 30, f'{path.name} has a truncated Vorbis identification header'
    channels = header[11]
    sample_rate = struct.unpack_from('<I', header, 12)[0]
    nominal_bitrate = struct.unpack_from('<i', header, 20)[0]
    return channels, sample_rate, nominal_bitrate

for slot in ['menu', 'gameplay', 'intense']:
    channels, sample_rate, nominal_bitrate = vorbis_identification(ROOT / 'assets' / 'music' / f'{slot}.ogg')
    assert channels == 2, f'{slot}.ogg must remain stereo'
    assert sample_rate == 44100, f'{slot}.ogg must remain 44.1 kHz'
    assert 0 < nominal_bitrate <= 192000, f'{slot}.ogg nominal Vorbis bitrate is too aggressive: {nominal_bitrate}'

print('web music backend architecture checks passed')
