cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FASTTRIS_VERSION)
string(STRIP "${FASTTRIS_VERSION}" FASTTRIS_VERSION)
if(NOT FASTTRIS_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain exactly MAJOR.MINOR.PATCH (for example 0.1.0). Got: '${FASTTRIS_VERSION}'")
endif()

project(FasTris VERSION ${FASTTRIS_VERSION} LANGUAGES CXX)

# The Windows release is a single self-contained FasTris.exe. SDL is already
# linked statically; use the static MSVC runtime as well so the release does
# not need a Visual C++ runtime DLL beside the executable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FASTTRIS_BUILD_APP "Build SDL3 app" ON)
option(FASTTRIS_BUILD_TESTS "Build deterministic core tests" ON)
option(FASTTRIS_BUILD_TOOLS "Build headless replay/sequence/benchmark tools" ON)
option(FASTTRIS_USE_SYSTEM_SDL "Use an installed/provided SDL3 package instead of FetchContent" OFF)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/fasttris/version.hpp"
    @ONLY
)

add_library(fasttris_core STATIC
    src/core/board.cpp
    src/core/game.cpp
    src/core/garbage.cpp
    src/core/replay.cpp
    src/core/rules.cpp
    src/core/scoring.cpp
    src/core/sha256.cpp
    src/core/tetromino.cpp
)
target_include_directories(fasttris_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
set_target_properties(fasttris_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(fasttris_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(fasttris_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(FASTTRIS_BUILD_APP)
    include(FetchContent)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    function(fastris_find_music_asset slot out_path out_ext)
        file(GLOB _candidates CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/music/${slot}.ogg"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/music/${slot}.mp3"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/music/${slot}.wav"
        )
        list(LENGTH _candidates _count)
        if(NOT _count EQUAL 1)
            message(FATAL_ERROR
                "Music slot '${slot}' requires exactly one source file: ${slot}.ogg, ${slot}.mp3, or ${slot}.wav. Found ${_count}.")
        endif()
        list(GET _candidates 0 _asset)
        get_filename_component(_ext "${_asset}" EXT)
        string(TOLOWER "${_ext}" _ext)
        set(${out_path} "${_asset}" PARENT_SCOPE)
        set(${out_ext} "${_ext}" PARENT_SCOPE)
    endfunction()

    fastris_find_music_asset(menu FASTTRIS_MUSIC_MENU FASTTRIS_MUSIC_MENU_EXT)
    fastris_find_music_asset(gameplay FASTTRIS_MUSIC_GAMEPLAY FASTTRIS_MUSIC_GAMEPLAY_EXT)
    fastris_find_music_asset(intense FASTTRIS_MUSIC_INTENSE FASTTRIS_MUSIC_INTENSE_EXT)
    set(FASTTRIS_MUSIC_EXTENSIONS
        "${FASTTRIS_MUSIC_MENU_EXT}"
        "${FASTTRIS_MUSIC_GAMEPLAY_EXT}"
        "${FASTTRIS_MUSIC_INTENSE_EXT}")
    if(".ogg" IN_LIST FASTTRIS_MUSIC_EXTENSIONS)
        FetchContent_Declare(fasttris_stb
            URL https://github.com/nothings/stb/archive/f1c79c02822848a9bed4315b12c8c8f3761e1296.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            SOURCE_SUBDIR __fasttris_header_only
        )
        FetchContent_MakeAvailable(fasttris_stb)
        set(FASTTRIS_HAS_OGG TRUE)
    endif()
    if(".mp3" IN_LIST FASTTRIS_MUSIC_EXTENSIONS)
        # Pinned after the 2026 dr_mp3 bounds/overflow fixes.
        FetchContent_Declare(fasttris_dr_libs
            URL https://github.com/mackron/dr_libs/archive/8a7258cc66b49387ad58cc5b81568982a3560d49.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            SOURCE_SUBDIR __fasttris_header_only
        )
        FetchContent_MakeAvailable(fasttris_dr_libs)
        set(FASTTRIS_HAS_MP3 TRUE)
    endif()

    set(FASTTRIS_GENERATED_MUSIC_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/music_assets.cpp")
    add_custom_command(
        OUTPUT "${FASTTRIS_GENERATED_MUSIC_CPP}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_music.py"
                --source-dir "${CMAKE_CURRENT_SOURCE_DIR}/assets/music"
                --output "${FASTTRIS_GENERATED_MUSIC_CPP}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_music.py"
            "${FASTTRIS_MUSIC_MENU}"
            "${FASTTRIS_MUSIC_GAMEPLAY}"
            "${FASTTRIS_MUSIC_INTENSE}"
        COMMENT "Embedding FasTris music assets"
        VERBATIM
    )

    if(EMSCRIPTEN)
        # SDL's callback main loop maps to requestAnimationFrame. SDL also
        # mounts this path with IDBFS when built with the option below.
        set(CMAKE_EXECUTABLE_SUFFIX ".html" CACHE INTERNAL "")
        set(SDL_EMSCRIPTEN_PERSISTENT_PATH "/storage" CACHE STRING "" FORCE)
    endif()

    if(ANDROID OR FASTTRIS_USE_SYSTEM_SDL)
        # Android's Gradle/Prefab build supplies SDL3 from the official AAR.
        find_package(SDL3 CONFIG REQUIRED)
    else()
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL3
            URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.14/SDL3-3.4.14.tar.gz
            URL_HASH SHA256=30d4aa2b3037718142b32dffd4e72f917ebb6cc5227150e7bb9c45efb2153aeb
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(SDL3)
    endif()

    set(FASTTRIS_APP_SOURCES
        src/app/main.cpp
        src/app/app_config.cpp
        src/app/renderer.cpp
        src/app/music_policy.cpp
        src/app/music_manager.cpp
        src/app/audio_codecs.cpp
        "${FASTTRIS_GENERATED_MUSIC_CPP}"
    )

    if(ANDROID)
        # SDLActivity expects the application entry point in libmain.so.
        add_library(main SHARED ${FASTTRIS_APP_SOURCES})
        set(FASTTRIS_APP_TARGET main)
    elseif(APPLE AND NOT IOS)
        add_executable(fasttris MACOSX_BUNDLE ${FASTTRIS_APP_SOURCES})
        set(FASTTRIS_APP_TARGET fasttris)
        set_target_properties(fasttris PROPERTIES
            OUTPUT_NAME "FasTris"
            MACOSX_BUNDLE_BUNDLE_NAME "FasTris"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.draconov.fastris"
            MACOSX_BUNDLE_BUNDLE_VERSION "${FASTTRIS_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${FASTTRIS_VERSION}"
        )
    else()
        add_executable(fasttris ${FASTTRIS_APP_SOURCES})
        set(FASTTRIS_APP_TARGET fasttris)
        set_target_properties(fasttris PROPERTIES OUTPUT_NAME "FasTris")
    endif()

    target_link_libraries(${FASTTRIS_APP_TARGET} PRIVATE fasttris_core SDL3::SDL3)
    target_include_directories(${FASTTRIS_APP_TARGET} PRIVATE src/app)
    if(FASTTRIS_HAS_OGG)
        target_include_directories(${FASTTRIS_APP_TARGET} PRIVATE "${fasttris_stb_SOURCE_DIR}")
        target_compile_definitions(${FASTTRIS_APP_TARGET} PRIVATE FASTTRIS_HAS_OGG=1)
    endif()
    if(FASTTRIS_HAS_MP3)
        target_include_directories(${FASTTRIS_APP_TARGET} PRIVATE "${fasttris_dr_libs_SOURCE_DIR}")
        target_compile_definitions(${FASTTRIS_APP_TARGET} PRIVATE FASTTRIS_HAS_MP3=1)
    endif()
    if(MSVC)
        target_compile_options(${FASTTRIS_APP_TARGET} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${FASTTRIS_APP_TARGET} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(EMSCRIPTEN)
        set_target_properties(fasttris PROPERTIES OUTPUT_NAME "index")
        target_link_options(fasttris PRIVATE
            "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/platform/web/shell.html"
            "SHELL:-sALLOW_MEMORY_GROWTH=1"
            "SHELL:-sMAXIMUM_MEMORY=1073741824"
            "SHELL:-sEXPORTED_RUNTIME_METHODS=ccall"
            "SHELL:-sEXPORTED_FUNCTIONS=_main,_malloc,_free"
        )
    endif()
endif()

if(FASTTRIS_BUILD_TOOLS AND NOT ANDROID AND NOT EMSCRIPTEN)
    add_executable(fasttris_verify src/tools/replay_verify.cpp)
    target_link_libraries(fasttris_verify PRIVATE fasttris_core)
    set_target_properties(fasttris_verify PROPERTIES OUTPUT_NAME "fastris_verify")

    add_executable(fasttris_sequence src/tools/sequence.cpp)
    target_link_libraries(fasttris_sequence PRIVATE fasttris_core)
    set_target_properties(fasttris_sequence PROPERTIES OUTPUT_NAME "fastris_sequence")

    add_executable(fasttris_bench src/tools/bench.cpp)
    target_link_libraries(fasttris_bench PRIVATE fasttris_core)
    set_target_properties(fasttris_bench PROPERTIES OUTPUT_NAME "fastris_bench")
endif()

if(FASTTRIS_BUILD_TESTS AND NOT ANDROID AND NOT EMSCRIPTEN)
    enable_testing()
    add_executable(fasttris_core_tests tests/core_tests.cpp)
    target_link_libraries(fasttris_core_tests PRIVATE fasttris_core)
    add_test(NAME fasttris_core COMMAND fasttris_core_tests)

    add_executable(fasttris_shader_math_tests tests/shader_math_tests.cpp)
    target_include_directories(fasttris_shader_math_tests PRIVATE src/app)
    add_test(NAME fasttris_shader_math COMMAND fasttris_shader_math_tests)

    add_executable(fasttris_music_policy_tests tests/music_policy_tests.cpp src/app/music_policy.cpp)
    target_include_directories(fasttris_music_policy_tests PRIVATE src/app)
    target_link_libraries(fasttris_music_policy_tests PRIVATE fasttris_core)
    add_test(NAME fasttris_music_policy COMMAND fasttris_music_policy_tests)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME fasttris_music_embed COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_embed_music.py")
    endif()

    if(FASTTRIS_BUILD_APP)
        add_executable(fasttris_app_config_music_tests tests/app_config_music_tests.cpp src/app/app_config.cpp)
        target_include_directories(fasttris_app_config_music_tests PRIVATE src/app)
        target_link_libraries(fasttris_app_config_music_tests PRIVATE SDL3::SDL3)
        add_test(NAME fasttris_app_config_music COMMAND fasttris_app_config_music_tests)
    endif()
endif()
