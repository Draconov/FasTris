#include "app_config.hpp"
#include "renderer.hpp"
#include "fasttris/game.hpp"
#include "fasttris/replay.hpp"
#include "fasttris/sha256.hpp"
#include "fasttris/version.hpp"
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <vector>

using namespace fasttris;
using namespace fasttris::app;

namespace {
enum class Screen { Menu, Game, Settings, Controls, SeedEntry, Help, Replay, CustomSetup };

std::uint64_t randomSeed() {
    std::random_device rd;
    const auto t = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return splitMix64((std::uint64_t(rd()) << 32) ^ rd() ^ t);
}

Mode modeFromMenu(int i) {
    static constexpr Mode modes[] = {
        Mode::Sprint40, Mode::Ultra120, Mode::Marathon, Mode::Zen,
        Mode::Cheese40, Mode::Finesse, Mode::SeedRace, Mode::Custom};
    return modes[std::clamp(i, 0, 7)];
}

bool parseMode(const std::string& s, Mode& mode) {
    if (s == "sprint") mode = Mode::Sprint40;
    else if (s == "ultra") mode = Mode::Ultra120;
    else if (s == "marathon") mode = Mode::Marathon;
    else if (s == "zen") mode = Mode::Zen;
    else if (s == "cheese") mode = Mode::Cheese40;
    else if (s == "finesse") mode = Mode::Finesse;
    else if (s == "seedrace") mode = Mode::SeedRace;
    else if (s == "custom") mode = Mode::Custom;
    else return false;
    return true;
}

std::uint64_t seedFromText(const std::string& s) {
    const auto hash = sha256(s);
    return std::stoull(hash.substr(0, 16), nullptr, 16);
}

Rules effectiveRules(const AppConfig& cfg, Mode mode) {
    Rules rules = cfg.rules;

    // Seed Race must compare like with like. Player handling (DAS/ARR/SDF/DCD)
    // stays personal, while placement-affecting rules are standardized.
    if (mode == Mode::SeedRace) {
        rules.handling.allow_180 = true;
        rules.handling.irs = true;
        rules.handling.ihs = true;
        rules.handling.lock_delay_ms = 500;
        rules.handling.max_lock_resets = 15;
        rules.ghost = true;
        rules.next_count = 5;
    }

    if (!rules.tournament) return rules;

    // Tournament lock changes only the rules used by the run. Personal settings
    // remain intact so toggling tournament mode cannot destroy a player's setup.
    rules.handling.allow_180 = false;
    rules.handling.irs = true;
    rules.handling.ihs = true;
    rules.handling.lock_delay_ms = 500;
    rules.handling.max_lock_resets = 15;
    rules.ghost = true;
    rules.next_count = 5;
    rules.garbage_cap = 8;
    rules.garbage_delay_ms = 500;
    rules.garbage_messiness_pct = 25;
    return rules;
}

std::string preferenceRoot() {
    char* raw = SDL_GetPrefPath("Draconov", "FasTris");
    if (!raw) return {};
    std::string out(raw);
    SDL_free(raw);
    return out;
}

struct ReplayViewer {
    Replay rep{};
    std::unique_ptr<Game> game;
    std::size_t index{};
    TimeUs playhead{};
    bool paused{};
    double speed{1.0};
    Uint64 last_wall_ns{};

    bool load(const std::string& path, std::string& err) {
        if (!loadReplay(path, rep, &err)) return false;
        reset(0);
        return true;
    }

    void reset(TimeUs target) {
        game = std::make_unique<Game>(rep.seed, rep.mode, rep.rules);
        index = 0;
        playhead = 0;
        seek(target);
        last_wall_ns = SDL_GetTicksNS();
    }

    void seek(TimeUs target) {
        target = std::clamp<TimeUs>(target, 0, rep.duration_us);
        if (!game || target < playhead) {
            game = std::make_unique<Game>(rep.seed, rep.mode, rep.rules);
            index = 0;
            playhead = 0;
        }
        while (index < rep.events.size() && rep.events[index].time_us <= target) {
            const auto& e = rep.events[index];
            game->advanceTo(e.time_us);
            if (e.down) game->press(e.action);
            else game->release(e.action);
            ++index;
        }
        game->advanceTo(target);
        playhead = target;
    }

    void tick(Uint64 now) {
        if (last_wall_ns == 0) last_wall_ns = now;
        if (!paused) {
            const auto delta = now - last_wall_ns;
            const auto add = static_cast<TimeUs>((delta / 1000.0) * speed);
            seek(std::min(rep.duration_us, playhead + add));
            if (playhead >= rep.duration_us) paused = true;
        }
        last_wall_ns = now;
    }

    std::vector<ReplayEvent> recent() const {
        std::vector<ReplayEvent> out;
        std::size_t i = index;
        while (i > 0 && out.size() < 8) {
            --i;
            out.push_back(rep.events[i]);
        }
        std::reverse(out.begin(), out.end());
        return out;
    }

    void nextPiece() {
        for (std::size_t i = index; i < rep.events.size(); ++i) {
            if (rep.events[i].down && rep.events[i].action == Action::HardDrop) {
                seek(rep.events[i].time_us);
                return;
            }
        }
        seek(rep.duration_us);
    }
};

struct RunSession {
    std::unique_ptr<Game> game;
    Replay replay{};
    Uint64 start_ns{};
    Uint64 paused_total_ns{};
    Uint64 pause_start_ns{};
    bool paused{};
    bool autosaved{};
    std::string status;
    std::vector<ReplayEvent> recent;

    void start(std::uint64_t seed, Mode mode, const Rules& rules, Uint64 now) {
        game = std::make_unique<Game>(seed, mode, rules);
        replay = {};
        replay.seed = seed;
        replay.mode = mode;
        replay.rules = rules;
        start_ns = now;
        paused_total_ns = 0;
        pause_start_ns = 0;
        paused = false;
        autosaved = false;
        status.clear();
        recent.clear();
    }

    TimeUs simAt(Uint64 ns) const {
        const Uint64 effective = paused ? pause_start_ns : ns;
        if (effective < start_ns + paused_total_ns) return 0;
        return static_cast<TimeUs>((effective - start_ns - paused_total_ns) / 1000);
    }

    void advance(Uint64 ns) {
        if (game && !paused) game->advanceTo(simAt(ns));
    }

    void togglePause(Uint64 ns) {
        if (!game) return;
        if (!paused) {
            game->advanceTo(simAt(ns));
            paused = true;
            pause_start_ns = ns;
        } else {
            paused_total_ns += ns - pause_start_ns;
            paused = false;
            pause_start_ns = 0;
        }
    }

    void record(TimeUs t, Action action, bool down) {
        replay.events.push_back({t, action, down});
        recent.push_back({t, action, down});
        if (recent.size() > 12) recent.erase(recent.begin());
    }

    void input(Uint64 ns, Action action, bool down) {
        if (!game || paused) return;
        status.clear();
        const auto t = simAt(ns);
        game->advanceTo(t);
        if (down) game->press(action);
        else game->release(action);
        record(t, action, down);
    }

    bool save(const std::string& path) {
        if (!game) return false;
        replay.duration_us = game->now();
        replay.final_hash = stateHash(*game);
        std::error_code ec;
        const auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
        std::string err;
        if (!saveReplay(replay, path, &err)) {
            status = "SAVE FAILED: " + err;
            return false;
        }
        status = "REPLAY SAVED";
        return true;
    }
};

Action keyAction(const AppConfig& cfg, SDL_Keycode key) {
    for (int i = 0; i < 8; ++i) {
        if (cfg.keys[i] == key) return static_cast<Action>(i);
    }
    return Action::Count;
}

Action padAction(const AppConfig& cfg, int button) {
    for (int i = 0; i < 8; ++i) {
        if (cfg.pads[i] == button) return static_cast<Action>(i);
    }
    return Action::Count;
}

void printHelp() {
    std::cout
        << "FasTris " << kVersion << "\n"
        << "  --version\n"
        << "  --seed N\n"
        << "  --daily YYYY-MM-DD\n"
        << "  --mode sprint|ultra|marathon|zen|cheese|finesse|seedrace|custom\n"
        << "  --tournament\n"
        << "  --replay FILE\n"
        << "  --verify FILE\n";
}

struct AppState {
    AppConfig cfg{defaultConfig()};
    SDL_Window* win{};
    SDL_Renderer* ren{};
    std::vector<SDL_Gamepad*> pads;

    Screen screen{Screen::Menu};
    int menu_sel{};
    int settings_sel{};
    int controls_sel{};
    int custom_sel{};
    bool rebinding{};
    bool wait_pad{};
    bool fullscreen{};

    std::uint64_t seed{randomSeed()};
    RunSession run;
    ReplayViewer viewer;
    std::string seed_text;
    std::string seed_error;
    std::string config_path;
    std::string last_replay_path;
    Uint64 frame_start{};

    void startRun(Mode mode, Uint64 now) {
        run.start(seed, mode, effectiveRules(cfg, mode), now);
        screen = Screen::Game;
    }

    bool lastReplayExists() const {
        std::error_code ec;
        return !last_replay_path.empty() && std::filesystem::exists(last_replay_path, ec);
    }

    bool initialize(int argc, char** argv, bool& should_exit) {
        should_exit = false;
        Mode start_mode = Mode::Sprint40;
        std::string replay_path;
        std::string verify_path;
        bool tournament = false;
        bool start_from_args = false;

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--version") {
                std::cout << kVersion << "\n";
                should_exit = true;
                return true;
            }
            if (arg == "--seed" && i + 1 < argc) {
                try {
                    seed = std::stoull(argv[++i]);
                } catch (...) {
                    std::cerr << "Invalid seed\n";
                    return false;
                }
            } else if (arg == "--daily" && i + 1 < argc) {
                seed = seedFromText(std::string("fasttris-daily-v1:") + argv[++i]);
            } else if (arg == "--mode" && i + 1 < argc) {
                if (!parseMode(argv[++i], start_mode)) {
                    std::cerr << "Invalid mode\n";
                    return false;
                }
                start_from_args = true;
            } else if (arg == "--tournament") {
                tournament = true;
                start_from_args = true;
            } else if (arg == "--replay" && i + 1 < argc) {
                replay_path = argv[++i];
            } else if (arg == "--verify" && i + 1 < argc) {
                verify_path = argv[++i];
            } else if (arg == "--help" || arg == "-h") {
                printHelp();
                should_exit = true;
                return true;
            }
        }

        if (!verify_path.empty()) {
            Replay replay;
            std::string err;
            std::string actual;
            if (!loadReplay(verify_path, replay, &err)) {
                std::cerr << "Replay load failed: " << err << "\n";
                return false;
            }
            const bool ok = verifyReplay(replay, &actual);
            std::cout << (ok ? "VERIFIED\n" : "FAILED\n")
                      << "expected: " << replay.final_hash << "\n"
                      << "actual:   " << actual << "\n";
            should_exit = true;
            return ok;
        }

        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
            return false;
        }

        const auto root = preferenceRoot();
        if (!root.empty()) {
            config_path = (std::filesystem::path(root) / "fastris.cfg").string();
            last_replay_path = (std::filesystem::path(root) / "replays" / "last.ftr").string();
        } else {
            config_path = "fastris.cfg";
            last_replay_path = "replays/last.ftr";
        }

        loadConfig(config_path, cfg);
        if (tournament) cfg.rules.tournament = true;

        const std::string title = std::string("FasTris ") + kVersion;
        win = SDL_CreateWindow(title.c_str(), 960, 720,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!win) {
            std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
            return false;
        }

        ren = SDL_CreateRenderer(win, nullptr);
        if (!ren) {
            std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
            return false;
        }

        SDL_SetRenderLogicalPresentation(ren, 960, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderVSync(ren, cfg.vsync ? 1 : 0);

        int pcount = 0;
        if (auto ids = SDL_GetGamepads(&pcount)) {
            for (int i = 0; i < pcount; ++i) {
                if (auto* pad = SDL_OpenGamepad(ids[i])) pads.push_back(pad);
            }
            SDL_free(ids);
        }

        if (!replay_path.empty()) {
            std::string err;
            if (viewer.load(replay_path, err)) screen = Screen::Replay;
            else std::cerr << "Replay load failed: " << err << "\n";
        } else if (start_from_args) {
            startRun(start_mode, SDL_GetTicksNS());
        }

        return true;
    }

    SDL_AppResult onEvent(SDL_Event& ev) {
        if (ev.type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;

        if (ev.type == SDL_EVENT_GAMEPAD_ADDED) {
            if (auto* pad = SDL_OpenGamepad(ev.gdevice.which)) pads.push_back(pad);
            return SDL_APP_CONTINUE;
        }
        if (ev.type == SDL_EVENT_GAMEPAD_REMOVED) {
            for (auto it = pads.begin(); it != pads.end();) {
                if (SDL_GetGamepadID(*it) == ev.gdevice.which) {
                    SDL_CloseGamepad(*it);
                    it = pads.erase(it);
                } else {
                    ++it;
                }
            }
            return SDL_APP_CONTINUE;
        }
        if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.key == SDLK_F11) {
            fullscreen = !fullscreen;
            SDL_SetWindowFullscreen(win, fullscreen);
            return SDL_APP_CONTINUE;
        }

        if (screen == Screen::SeedEntry) {
            if (ev.type == SDL_EVENT_TEXT_INPUT) {
                for (const char* c = ev.text.text; *c; ++c) {
                    if (*c >= '0' && *c <= '9' && seed_text.size() < 20) seed_text.push_back(*c);
                }
            } else if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                if (ev.key.key == SDLK_BACKSPACE && !seed_text.empty()) {
                    seed_text.pop_back();
                } else if (ev.key.key == SDLK_ESCAPE) {
                    SDL_StopTextInput(win);
                    screen = Screen::Menu;
                    seed_error.clear();
                } else if (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER) {
                    try {
                        seed = std::stoull(seed_text);
                        SDL_StopTextInput(win);
                        screen = Screen::Menu;
                        seed_error.clear();
                    } catch (...) {
                        seed_error = "INVALID UINT64 SEED";
                    }
                }
            }
            return SDL_APP_CONTINUE;
        }

        if (screen == Screen::Help) {
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.key == SDLK_ESCAPE) {
                screen = Screen::Menu;
            }
            return SDL_APP_CONTINUE;
        }

        if (screen == Screen::Menu) {
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                const auto key = ev.key.key;
                if (key == SDLK_UP) menu_sel = (menu_sel + 11) % 12;
                else if (key == SDLK_DOWN) menu_sel = (menu_sel + 1) % 12;
                else if (key == SDLK_R) seed = randomSeed();
                else if (key == SDLK_E) {
                    seed_text = std::to_string(seed);
                    seed_error.clear();
                    screen = Screen::SeedEntry;
                    SDL_StartTextInput(win);
                } else if (key == SDLK_T) {
                    cfg.rules.tournament = !cfg.rules.tournament;
                    saveConfig(config_path, cfg);
                } else if (key == SDLK_H) {
                    screen = Screen::Help;
                } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                    if (menu_sel < 7) {
                        startRun(modeFromMenu(menu_sel), ev.key.timestamp);
                    } else if (menu_sel == 7) {
                        custom_sel = 0;
                        screen = Screen::CustomSetup;
                    } else if (menu_sel == 8) {
                        screen = Screen::Settings;
                    } else if (menu_sel == 9) {
                        screen = Screen::Controls;
                    } else if (menu_sel == 10) {
                        std::string err;
                        if (lastReplayExists() && viewer.load(last_replay_path, err)) {
                            screen = Screen::Replay;
                        }
                    } else {
                        return SDL_APP_SUCCESS;
                    }
                }
            }
            return SDL_APP_CONTINUE;
        }

        if (screen == Screen::CustomSetup) {
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                const auto key = ev.key.key;
                if (key == SDLK_ESCAPE) {
                    saveConfig(config_path, cfg);
                    screen = Screen::Menu;
                } else if (key == SDLK_UP) {
                    custom_sel = (custom_sel + 4) % 5;
                } else if (key == SDLK_DOWN) {
                    custom_sel = (custom_sel + 1) % 5;
                } else if (key == SDLK_LEFT || key == SDLK_RIGHT) {
                    const int delta = key == SDLK_RIGHT ? 1 : -1;
                    auto& r = cfg.rules;
                    if (custom_sel == 0) {
                        static constexpr int values[] = {0, 2000, 1000, 500, 250, 100, 50, 16, 8, 1};
                        int at = 0;
                        for (int i = 0; i < 10; ++i) if (values[i] == r.custom_gravity_ms) at = i;
                        at = std::clamp(at + delta, 0, 9);
                        r.custom_gravity_ms = values[at];
                    } else if (custom_sel == 1) {
                        static constexpr int values[] = {0, 20, 40, 100, 150, 200, 1000};
                        int at = 0;
                        for (int i = 0; i < 7; ++i) if (values[i] == r.custom_line_goal) at = i;
                        at = std::clamp(at + delta, 0, 6);
                        r.custom_line_goal = values[at];
                    } else if (custom_sel == 2) {
                        static constexpr int values[] = {0, 30, 60, 120, 180, 300, 600};
                        int at = 0;
                        for (int i = 0; i < 7; ++i) if (values[i] == r.custom_time_limit_s) at = i;
                        at = std::clamp(at + delta, 0, 6);
                        r.custom_time_limit_s = values[at];
                    } else if (custom_sel == 3) {
                        r.custom_start_garbage = std::clamp(r.custom_start_garbage + delta, 0, 12);
                    }
                } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                    if (custom_sel == 4) {
                        saveConfig(config_path, cfg);
                        startRun(Mode::Custom, ev.key.timestamp);
                    }
                }
            }
            return SDL_APP_CONTINUE;
        }

        if (screen == Screen::Settings) {
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                const auto key = ev.key.key;
                if (key == SDLK_ESCAPE) {
                    saveConfig(config_path, cfg);
                    screen = Screen::Menu;
                } else if (key == SDLK_UP) {
                    settings_sel = (settings_sel + 12) % 13;
                } else if (key == SDLK_DOWN) {
                    settings_sel = (settings_sel + 1) % 13;
                } else if (key == SDLK_R) {
                    const bool tournament = cfg.rules.tournament;
                    cfg.rules = Rules{};
                    cfg.rules.tournament = tournament;
                } else if (key == SDLK_LEFT || key == SDLK_RIGHT) {
                    const int delta = key == SDLK_RIGHT ? 1 : -1;
                    auto& handling = cfg.rules.handling;
                    const bool locked = cfg.rules.tournament && settings_sel >= 4 && settings_sel <= 10;
                    if (!locked) {
                        switch (settings_sel) {
                            case 0: handling.das_ms = std::clamp(handling.das_ms + delta * 5, 0, 1000); break;
                            case 1: handling.arr_ms = std::clamp(handling.arr_ms + delta, 0, 500); break;
                            case 2: handling.sdf = std::clamp(handling.sdf + delta, 0, 200); break;
                            case 3: handling.dcd_ms = std::clamp(handling.dcd_ms + delta * 5, 0, 1000); break;
                            case 4: handling.lock_delay_ms = std::clamp(handling.lock_delay_ms + delta * 10, 0, 2000); break;
                            case 5: handling.max_lock_resets = std::clamp(handling.max_lock_resets + delta, 0, 100); break;
                            case 6: handling.allow_180 = !handling.allow_180; break;
                            case 7: handling.irs = !handling.irs; break;
                            case 8: handling.ihs = !handling.ihs; break;
                            case 9: cfg.rules.ghost = !cfg.rules.ghost; break;
                            case 10: cfg.rules.next_count = std::clamp(cfg.rules.next_count + delta, 1, 8); break;
                            case 11:
                                cfg.vsync = !cfg.vsync;
                                SDL_SetRenderVSync(ren, cfg.vsync ? 1 : 0);
                                break;
                            case 12: {
                                static constexpr int caps[] = {0, 60, 120, 144, 165, 240, 360, 480, 500, 1000};
                                int index = 0;
                                for (int i = 0; i < 10; ++i) if (caps[i] == cfg.fps_cap) index = i;
                                index = std::clamp(index + delta, 0, 9);
                                cfg.fps_cap = caps[index];
                                break;
                            }
                            default: break;
                        }
                    }
                }
            }
            return SDL_APP_CONTINUE;
        }

        if (screen == Screen::Controls) {
            if (rebinding) {
                if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                    if (ev.key.key == SDLK_ESCAPE) {
                        rebinding = false;
                        wait_pad = false;
                    } else if (!wait_pad && ev.key.key == SDLK_G) {
                        wait_pad = true;
                    } else if (!wait_pad) {
                        cfg.keys[controls_sel] = ev.key.key;
                        rebinding = false;
                        saveConfig(config_path, cfg);
                    }
                } else if (wait_pad && ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                    cfg.pads[controls_sel] = ev.gbutton.button;
                    rebinding = false;
                    wait_pad = false;
                    saveConfig(config_path, cfg);
                }
                return SDL_APP_CONTINUE;
            }

            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                const auto key = ev.key.key;
                if (key == SDLK_ESCAPE) {
                    screen = Screen::Menu;
                    saveConfig(config_path, cfg);
                } else if (key == SDLK_UP) {
                    controls_sel = (controls_sel + 7) % 8;
                } else if (key == SDLK_DOWN) {
                    controls_sel = (controls_sel + 1) % 8;
                } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                    rebinding = true;
                    wait_pad = false;
                } else if (key == SDLK_G) {
                    rebinding = true;
                    wait_pad = true;
                }
            }
            return SDL_APP_CONTINUE;
        }

        if (screen == Screen::Replay) {
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                const auto key = ev.key.key;
                if (key == SDLK_ESCAPE) screen = Screen::Menu;
                else if (key == SDLK_SPACE) viewer.paused = !viewer.paused;
                else if (key == SDLK_LEFT) { viewer.seek(viewer.playhead - 1000000); viewer.paused = true; }
                else if (key == SDLK_RIGHT) { viewer.seek(viewer.playhead + 1000000); viewer.paused = true; }
                else if (key == SDLK_1) viewer.speed = 1;
                else if (key == SDLK_2) viewer.speed = 2;
                else if (key == SDLK_4) viewer.speed = 4;
                else if (key == SDLK_8) viewer.speed = 8;
                else if (key == SDLK_N) { viewer.nextPiece(); viewer.paused = true; }
            }
            return SDL_APP_CONTINUE;
        }

        if (screen == Screen::Game) {
            Uint64 timestamp = 0;
            if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) timestamp = ev.key.timestamp;
            else if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || ev.type == SDL_EVENT_GAMEPAD_BUTTON_UP) timestamp = ev.gbutton.timestamp;
            else timestamp = SDL_GetTicksNS();

            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                const auto key = ev.key.key;
                if (key == SDLK_ESCAPE) {
                    screen = Screen::Menu;
                    return SDL_APP_CONTINUE;
                }
                if (key == cfg.keys[static_cast<int>(Action::Pause)]) {
                    if (run.game->rules().tournament) run.status = "PAUSE DISABLED IN TOURNAMENT";
                    else run.togglePause(timestamp);
                    return SDL_APP_CONTINUE;
                }
                if (key == cfg.keys[static_cast<int>(Action::Restart)]) {
                    run.start(seed, run.game->mode(), effectiveRules(cfg, run.game->mode()), timestamp);
                    return SDL_APP_CONTINUE;
                }
                if (key == SDLK_F6) {
                    run.advance(timestamp);
                    run.save(last_replay_path);
                    return SDL_APP_CONTINUE;
                }
                const auto action = keyAction(cfg, key);
                if (action != Action::Count) run.input(timestamp, action, true);
            } else if (ev.type == SDL_EVENT_KEY_UP) {
                const auto action = keyAction(cfg, ev.key.key);
                if (action != Action::Count) run.input(timestamp, action, false);
            } else if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                const int button = ev.gbutton.button;
                if (button == cfg.pads[static_cast<int>(Action::Pause)]) {
                    if (run.game->rules().tournament) run.status = "PAUSE DISABLED IN TOURNAMENT";
                    else run.togglePause(timestamp);
                    return SDL_APP_CONTINUE;
                }
                if (button == cfg.pads[static_cast<int>(Action::Restart)]) {
                    run.start(seed, run.game->mode(), effectiveRules(cfg, run.game->mode()), timestamp);
                    return SDL_APP_CONTINUE;
                }
                const auto action = padAction(cfg, button);
                if (action != Action::Count) run.input(timestamp, action, true);
            } else if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
                const auto action = padAction(cfg, ev.gbutton.button);
                if (action != Action::Count) run.input(timestamp, action, false);
            }
        }

        return SDL_APP_CONTINUE;
    }

    SDL_AppResult iterate() {
        frame_start = SDL_GetTicksNS();
        const auto now = frame_start;

        if (screen == Screen::Game && run.game) {
            run.advance(now);
            if ((run.game->gameOver() || run.game->complete()) && !run.autosaved) {
                run.save(last_replay_path);
                run.autosaved = true;
            }
            RenderInfo info;
            info.seed = seed;
            info.paused = run.paused;
            info.status = run.status;
            info.recent_inputs = run.recent;
            renderGame(ren, *run.game, info);
        } else if (screen == Screen::Replay && viewer.game) {
            viewer.tick(now);
            RenderInfo info;
            info.seed = viewer.rep.seed;
            info.replay_mode = true;
            info.replay_speed = viewer.speed;
            info.replay_paused = viewer.paused;
            info.recent_inputs = viewer.recent();
            info.status = viewer.rep.final_hash.empty()
                              ? "UNVERIFIED REPLAY"
                              : (verifyReplay(viewer.rep) ? "REPLAY VERIFIED" : "REPLAY HASH FAILED");
            renderGame(ren, *viewer.game, info);
        } else if (screen == Screen::Menu) {
            renderMenu(ren, menu_sel, seed, cfg.rules.tournament, lastReplayExists());
        } else if (screen == Screen::Settings) {
            renderSettings(ren, cfg, settings_sel);
        } else if (screen == Screen::CustomSetup) {
            renderCustomSetup(ren, cfg, custom_sel);
        } else if (screen == Screen::Controls) {
            renderControls(ren, cfg, controls_sel, rebinding, wait_pad);
        } else if (screen == Screen::SeedEntry) {
            renderSeedEntry(ren, seed_text, seed_error);
        } else if (screen == Screen::Help) {
            renderHelp(ren);
        }

        SDL_RenderPresent(ren);

#ifndef __EMSCRIPTEN__
        if (!cfg.vsync && cfg.fps_cap > 0) {
            const Uint64 target = 1000000000ULL / static_cast<Uint64>(cfg.fps_cap);
            const Uint64 spent = SDL_GetTicksNS() - frame_start;
            if (spent < target) SDL_DelayPrecise(target - spent);
        }
#endif

        return SDL_APP_CONTINUE;
    }

    void shutdown() {
        if (!config_path.empty()) saveConfig(config_path, cfg);
        for (auto* pad : pads) SDL_CloseGamepad(pad);
        pads.clear();
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        ren = nullptr;
        win = nullptr;
    }
};
} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    auto* state = new AppState();
    *appstate = state;
    bool should_exit = false;
    if (!state->initialize(argc, argv, should_exit)) return SDL_APP_FAILURE;
    return should_exit ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    if (!appstate || !event) return SDL_APP_FAILURE;
    return static_cast<AppState*>(appstate)->onEvent(*event);
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    if (!appstate) return SDL_APP_FAILURE;
    return static_cast<AppState*>(appstate)->iterate();
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    auto* state = static_cast<AppState*>(appstate);
    if (!state) return;
    state->shutdown();
    delete state;
}
