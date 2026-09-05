#!/usr/bin/env python3
from pathlib import Path
import re

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


# Native runtime audio failures must latch and tear down only the music stream.
for marker in ['std::atomic<bool> fatal_error', 'fatal_error.store(true', 'SDL_DestroyAudioStream(impl_->output)', 'leave gameplay/input untouched']:
    assert marker in native_backend, f'native fail-open music marker missing: {marker}'

print('web music backend architecture checks passed')
