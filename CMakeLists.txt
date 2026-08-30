cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FASTTRIS_VERSION)
string(STRIP "${FASTTRIS_VERSION}" FASTTRIS_VERSION)
if(NOT FASTTRIS_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain exactly MAJOR.MINOR.PATCH (for example 0.1.0). Got: '${FASTTRIS_VERSION}'")
endif()

project(FasTris VERSION ${FASTTRIS_VERSION} LANGUAGES CXX)

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
        include(FetchContent)
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
endif()
