#include "app_config.hpp"
#include "renderer.hpp"
#include "fasttris/game.hpp"
#include "fasttris/replay.hpp"
#include "fasttris/sha256.hpp"
#include "fasttris/version.hpp"
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <limits>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <vector>

using namespace fasttris;
using namespace fasttris::app;

namespace {
enum class Screen { Menu, Game, Settings, Controls, Miscellaneous, Help, Replay, ReplayMenu, SandboxSetup };

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
    else if (s == "sandbox") mode = Mode::Custom;
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

enum class ReplayDialogAction { Load, Save };
struct ReplayDialogResult {
    ReplayDialogAction action{ReplayDialogAction::Load};
    std::string path;
    std::string error;
    bool canceled{};
};
struct ReplayDialogMailbox {
    std::mutex mutex;
    std::optional<ReplayDialogResult> pending;
};
struct ReplayDialogContext {
    std::shared_ptr<ReplayDialogMailbox> mailbox;
    ReplayDialogAction action{ReplayDialogAction::Load};
};

static const SDL_DialogFileFilter kReplayFilters[]={{"FasTris replay","ftr"}};

void SDLCALL replayDialogCallback(void* userdata,const char* const* filelist,int) {
    std::unique_ptr<ReplayDialogContext> context(static_cast<ReplayDialogContext*>(userdata));
    ReplayDialogResult result;
    result.action=context->action;
    if(!filelist) result.error=SDL_GetError();
    else if(!filelist[0]) result.canceled=true;
    else result.path=filelist[0];
    std::lock_guard<std::mutex> lock(context->mailbox->mutex);
    context->mailbox->pending=std::move(result);
}

bool loadReplayWithSDL(const std::string& path,Replay& replay,std::string& err) {
    SDL_IOStream* io=SDL_IOFromFile(path.c_str(),"rb");
    if(!io){err=SDL_GetError();return false;}
    size_t size=0;
    void* raw=SDL_LoadFile_IO(io,&size,true);
    if(!raw){err=SDL_GetError();return false;}
    constexpr size_t kMaxReplayBytes=32u*1024u*1024u;
    if(size>kMaxReplayBytes){SDL_free(raw);err="replay file is too large";return false;}
    std::string text(static_cast<const char*>(raw),size);
    SDL_free(raw);
    return deserializeReplay(text,replay,&err);
}

bool saveReplayWithSDL(const Replay& replay,const std::string& path,std::string& err) {
    std::string target=path;
    if(target.find("://")==std::string::npos){
        std::filesystem::path p(target);
        if(p.extension().empty()) target += ".ftr";
    }
    const std::string text=serializeReplay(replay);
    SDL_IOStream* io=SDL_IOFromFile(target.c_str(),"wb");
    if(!io){err=SDL_GetError();return false;}
    if(!SDL_SaveFile_IO(io,text.data(),text.size(),true)){err=SDL_GetError();return false;}
    return true;
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

    void load(Replay replay) {
        rep = std::move(replay);
        reset(0);
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

    Replay snapshot() const {
        Replay out=replay;
        if(game){out.duration_us=game->now();out.final_hash=stateHash(*game);}
        return out;
    }

    bool save(const std::string& path) {
        if (!game) return false;
        replay=snapshot();
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
        << "  --mode sprint|ultra|marathon|zen|cheese|finesse|seedrace|sandbox\n"
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
    int replay_menu_sel{};
    bool rebinding{};
    bool wait_pad{};
    bool fullscreen{};

    std::uint64_t seed{randomSeed()};
    RunSession run;
    ReplayViewer viewer;
    bool settings_number_editing{};
    bool settings_number_replace_on_type{};
    std::string settings_number_text;
    std::string settings_status;
    std::string config_path;
    std::string last_replay_path;
    std::string replay_status;
    std::shared_ptr<ReplayDialogMailbox> replay_dialog_mailbox{std::make_shared<ReplayDialogMailbox>()};
    bool replay_dialog_open{};
    bool replay_dialog_paused_run{};
    bool pending_save_from_game{};
    std::optional<Replay> pending_replay_save;
    Uint64 frame_start{};

    void startRun(Mode mode, Uint64 now) {
        run.start(seed, mode, effectiveRules(cfg, mode), now);
        screen = Screen::Game;
    }

    bool lastReplayExists() const {
        std::error_code ec;
        return !last_replay_path.empty() && std::filesystem::exists(last_replay_path, ec);
    }

    void openReplayLoadDialog() {
        if(replay_dialog_open)return;
        replay_dialog_open=true;
        replay_status="CHOOSE A REPLAY FILE";
        auto* context=new ReplayDialogContext{replay_dialog_mailbox,ReplayDialogAction::Load};
        SDL_ShowOpenFileDialog(replayDialogCallback,context,win,kReplayFilters,1,nullptr,false);
    }

    void openReplaySaveDialog(const Replay& replay,Uint64 now,bool from_game) {
        if(replay_dialog_open)return;
        pending_replay_save=replay;
        pending_save_from_game=from_game;
        replay_dialog_open=true;
        replay_dialog_paused_run=false;
        if(from_game&&run.game&&!run.paused){
            if(run.game->rules().tournament&&!run.game->complete()&&!run.game->gameOver()){
                replay_dialog_open=false;
                pending_replay_save.reset();
                pending_save_from_game=false;
                run.status="SAVE FILE DISABLED DURING TOURNAMENT RUN";
                return;
            }
            run.togglePause(now);
            replay_dialog_paused_run=true;
        }
        if(from_game)run.status="CHOOSE REPLAY FILE";
        else replay_status="CHOOSE REPLAY FILE";
        auto* context=new ReplayDialogContext{replay_dialog_mailbox,ReplayDialogAction::Save};
        SDL_ShowSaveFileDialog(replayDialogCallback,context,win,kReplayFilters,1,nullptr);
    }

    void processReplayDialog() {
        std::optional<ReplayDialogResult> result;
        {
            std::lock_guard<std::mutex> lock(replay_dialog_mailbox->mutex);
            if(replay_dialog_mailbox->pending){result=std::move(replay_dialog_mailbox->pending);replay_dialog_mailbox->pending.reset();}
        }
        if(!result)return;
        replay_dialog_open=false;

        if(result->action==ReplayDialogAction::Load){
            if(result->canceled){replay_status="LOAD CANCELED";return;}
            if(!result->error.empty()){replay_status="LOAD DIALOG FAILED: "+result->error;return;}
            Replay loaded;
            std::string err;
            if(!loadReplayWithSDL(result->path,loaded,err)){replay_status="LOAD FAILED: "+err;return;}
            viewer.load(std::move(loaded));
            replay_status.clear();
            screen=Screen::Replay;
            return;
        }

        const bool resume_run=replay_dialog_paused_run;
        replay_dialog_paused_run=false;
        if(result->canceled){
            if(pending_save_from_game)run.status="SAVE CANCELED";else replay_status="SAVE CANCELED";
        }else if(!result->error.empty()){
            if(pending_save_from_game)run.status="SAVE DIALOG FAILED: "+result->error;else replay_status="SAVE DIALOG FAILED: "+result->error;
        }else if(pending_replay_save){
            std::string err;
            const bool ok=saveReplayWithSDL(*pending_replay_save,result->path,err);
            if(pending_save_from_game)run.status=ok?"REPLAY SAVED TO FILE":"SAVE FAILED: "+err;
            else replay_status=ok?"REPLAY SAVED TO FILE":"SAVE FAILED: "+err;
        }
        pending_replay_save.reset();
        pending_save_from_game=false;
        if(resume_run&&run.game&&run.paused)run.togglePause(SDL_GetTicksNS());
    }

    bool settingLocked(int item) const {
        return cfg.rules.tournament && item >= SettingLock && item <= SettingNext;
    }

    static bool numericSetting(int item) {
        switch (item) {
            case SettingDas: case SettingArr: case SettingSdf: case SettingDcd:
            case SettingLock: case SettingResets: case SettingNext:
            case SettingFpsCap: case SettingSeed:
                return true;
            default:
                return false;
        }
    }

    void beginSettingNumberEdit() {
        if (!numericSetting(settings_sel)) return;
        if (settingLocked(settings_sel)) {
            settings_status = "LOCKED BY TOURNAMENT";
            return;
        }
        const auto& h = cfg.rules.handling;
        switch (settings_sel) {
            case SettingDas: settings_number_text = std::to_string(h.das_ms); break;
            case SettingArr: settings_number_text = std::to_string(h.arr_ms); break;
            case SettingSdf: settings_number_text = std::to_string(h.sdf); break;
            case SettingDcd: settings_number_text = std::to_string(h.dcd_ms); break;
            case SettingLock: settings_number_text = std::to_string(h.lock_delay_ms); break;
            case SettingResets: settings_number_text = std::to_string(h.max_lock_resets); break;
            case SettingNext: settings_number_text = std::to_string(cfg.rules.next_count); break;
            case SettingFpsCap: settings_number_text = std::to_string(cfg.fps_cap); break;
            case SettingSeed: settings_number_text = std::to_string(seed); break;
            default: return;
        }
        settings_status.clear();
        settings_number_editing = true;
        settings_number_replace_on_type = true;
        SDL_StartTextInput(win);
    }

    void cancelSettingNumberEdit() {
        if (!settings_number_editing) return;
        settings_number_editing = false;
        settings_number_replace_on_type = false;
        settings_number_text.clear();
        settings_status.clear();
        SDL_StopTextInput(win);
    }

    bool applySettingNumberEdit() {
        if (!settings_number_editing || settings_number_text.empty()) {
            settings_status = "ENTER A NUMBER";
            return false;
        }
        try {
            std::size_t used = 0;
            const auto value = std::stoull(settings_number_text, &used, 10);
            if (used != settings_number_text.size()) { settings_status = "INVALID NUMBER"; return false; }
            auto assignInt = [&](int lo, int hi, int& target, const char* range) {
                if (value > static_cast<unsigned long long>(hi) || value < static_cast<unsigned long long>(lo)) {
                    settings_status = std::string("VALID RANGE: ") + range;
                    return false;
                }
                target = static_cast<int>(value);
                return true;
            };

            bool ok = false;
            auto& h = cfg.rules.handling;
            switch (settings_sel) {
                case SettingDas: ok = assignInt(0, 1000, h.das_ms, "0-1000"); break;
                case SettingArr: ok = assignInt(0, 500, h.arr_ms, "0-500"); break;
                case SettingSdf: ok = assignInt(0, 200, h.sdf, "0-200"); break;
                case SettingDcd: ok = assignInt(0, 1000, h.dcd_ms, "0-1000"); break;
                case SettingLock: ok = assignInt(0, 2000, h.lock_delay_ms, "0-2000"); break;
                case SettingResets: ok = assignInt(0, 100, h.max_lock_resets, "0-100"); break;
                case SettingNext: ok = assignInt(1, 8, cfg.rules.next_count, "1-8"); break;
                case SettingFpsCap: ok = assignInt(0, 1000, cfg.fps_cap, "0-1000"); break;
                case SettingSeed: seed = static_cast<std::uint64_t>(value); ok = true; break;
                default: break;
            }
            if (!ok) return false;
        } catch (...) {
            settings_status = settings_sel == SettingSeed ? "INVALID UINT64 SEED" : "INVALID NUMBER";
            return false;
        }

        settings_number_editing = false;
        settings_number_replace_on_type = false;
        settings_number_text.clear();
        settings_status = "VALUE APPLIED";
        SDL_StopTextInput(win);
        saveConfig(config_path, cfg);
        return true;
    }

    void adjustSetting(int delta) {
        if (settingLocked(settings_sel)) {
            settings_status = "LOCKED BY TOURNAMENT";
            return;
        }
        auto& h = cfg.rules.handling;
        switch (settings_sel) {
            case SettingDas: h.das_ms = std::clamp(h.das_ms + delta * 5, 0, 1000); break;
            case SettingArr: h.arr_ms = std::clamp(h.arr_ms + delta, 0, 500); break;
            case SettingSdf: h.sdf = std::clamp(h.sdf + delta, 0, 200); break;
            case SettingDcd: h.dcd_ms = std::clamp(h.dcd_ms + delta * 5, 0, 1000); break;
            case SettingLock: h.lock_delay_ms = std::clamp(h.lock_delay_ms + delta * 10, 0, 2000); break;
            case SettingResets: h.max_lock_resets = std::clamp(h.max_lock_resets + delta, 0, 100); break;
            case SettingRotate180: h.allow_180 = !h.allow_180; break;
            case SettingIrs: h.irs = !h.irs; break;
            case SettingIhs: h.ihs = !h.ihs; break;
            case SettingGhost: cfg.rules.ghost = !cfg.rules.ghost; break;
            case SettingNext: cfg.rules.next_count = std::clamp(cfg.rules.next_count + delta, 1, 8); break;
            case SettingShowInputs: cfg.show_inputs = !cfg.show_inputs; break;
            case SettingVsync:
                cfg.vsync = !cfg.vsync;
                SDL_SetRenderVSync(ren, cfg.vsync ? 1 : 0);
                break;
            case SettingFpsCap: {
                static constexpr int caps[] = {0, 60, 120, 144, 165, 240, 360, 480, 500, 1000};
                int next = cfg.fps_cap;
                if (delta > 0) {
                    for (int cap : caps) if (cap > cfg.fps_cap) { next = cap; break; }
                } else {
                    for (int i = static_cast<int>(sizeof(caps)/sizeof(caps[0])) - 1; i >= 0; --i) {
                        if (caps[i] < cfg.fps_cap) { next = caps[i]; break; }
                    }
                }
                cfg.fps_cap = next;
                break;
            }
            case SettingTournament: cfg.rules.tournament = !cfg.rules.tournament; break;
            case SettingSeed:
                if (delta > 0 && seed < std::numeric_limits<std::uint64_t>::max()) ++seed;
                else if (delta < 0 && seed > 0) --seed;
                break;
            default: return;
        }
        settings_status.clear();
        saveConfig(config_path, cfg);
    }

    void activateSetting() {
        if (numericSetting(settings_sel)) {
            beginSettingNumberEdit();
            return;
        }
        if (settingLocked(settings_sel)) {
            settings_status = "LOCKED BY TOURNAMENT";
            return;
        }
        switch (settings_sel) {
            case SettingRotate180: case SettingIrs: case SettingIhs: case SettingGhost:
            case SettingShowInputs: case SettingVsync: case SettingTournament:
                adjustSetting(1);
                break;
            case SettingRandomSeed:
                seed = randomSeed();
                settings_status = "NEW RANDOM SEED";
                break;
            case SettingControls:
                controls_sel = 0;
                settings_status.clear();
                screen = Screen::Controls;
                break;
            case SettingMiscellaneous:
                settings_status.clear();
                screen = Screen::Miscellaneous;
                break;
            case SettingReset:
                resetSettings(cfg);
                SDL_SetRenderVSync(ren, cfg.vsync ? 1 : 0);
                saveConfig(config_path, cfg);
                settings_status = "SETTINGS RESET";
                break;
            default:
                break;
        }
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

        int initial_w = 1100;
        int initial_h = 800;
        SDL_Rect usable{};
        const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
        if (primary != 0 && SDL_GetDisplayUsableBounds(primary, &usable)) {
            initial_w = std::clamp(static_cast<int>(usable.w * 0.76f), 640, 1280);
            initial_h = std::clamp(static_cast<int>(usable.h * 0.84f), 480, 900);
            initial_w = std::min(initial_w, std::max(480, usable.w - 40));
            initial_h = std::min(initial_h, std::max(360, usable.h - 40));
        }

        const std::string title = std::string("FasTris ") + kVersion;
        win = SDL_CreateWindow(title.c_str(), initial_w, initial_h,
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

        // Render directly to the real drawable size. The renderer computes a
        // responsive canvas every frame, so widescreen/fullscreen windows use
        // their extra space instead of being forced into a 4:3 letterbox.
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
#if defined(__EMSCRIPTEN__)
        // SDL 3.4+ can own the browser canvas size and emit resize events as
        // the browser viewport changes. This keeps the web build genuinely
        // responsive instead of merely CSS-scaling a fixed backbuffer.
        SDL_SetWindowFillDocument(win, true);
#else
        SDL_SetWindowMinimumSize(win, 480, 360);
#endif
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
        if(replay_dialog_open)return SDL_APP_CONTINUE;

        if (screen == Screen::Help) {
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.key == SDLK_ESCAPE) {
                screen = Screen::Menu;
            }
            return SDL_APP_CONTINUE;
        }

        if (screen == Screen::Menu) {
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                const auto key = ev.key.key;
                if (key == SDLK_UP) menu_sel = (menu_sel + 10) % 11;
                else if (key == SDLK_DOWN) menu_sel = (menu_sel + 1) % 11;
                else if (key == SDLK_H) screen = Screen::Help;
                else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                    if (menu_sel < 7) {
                        startRun(modeFromMenu(menu_sel), ev.key.timestamp);
                    } else if (menu_sel == 7) {
                        custom_sel = 0;
                        screen = Screen::SandboxSetup;
                    } else if (menu_sel == 8) {
                        settings_sel = 0;
                        settings_status.clear();
                        screen = Screen::Settings;
                    } else if (menu_sel == 9) {
                        replay_menu_sel = 0;
                        replay_status.clear();
                        screen = Screen::ReplayMenu;
                    } else {
                        return SDL_APP_SUCCESS;
                    }
                }
            }
            return SDL_APP_CONTINUE;
        }

        if (screen == Screen::ReplayMenu) {
            if(ev.type==SDL_EVENT_KEY_DOWN&&!ev.key.repeat){
                const auto key=ev.key.key;
                if(key==SDLK_ESCAPE){screen=Screen::Menu;replay_status.clear();}
                else if(key==SDLK_UP)replay_menu_sel=(replay_menu_sel+2)%3;
                else if(key==SDLK_DOWN)replay_menu_sel=(replay_menu_sel+1)%3;
                else if(key==SDLK_RETURN||key==SDLK_KP_ENTER){
                    if(replay_menu_sel==0){
                        std::string err;
                        if(lastReplayExists()&&viewer.load(last_replay_path,err)){replay_status.clear();screen=Screen::Replay;}
                        else replay_status=lastReplayExists()?"LOAD FAILED: "+err:"NO LAST REPLAY";
                    }else if(replay_menu_sel==1){
                        openReplayLoadDialog();
                    }else{
                        screen=Screen::Menu;
                        replay_status.clear();
                    }
                }
            }
            return SDL_APP_CONTINUE;
        }

        if (screen == Screen::SandboxSetup) {
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
            if (settings_number_editing) {
                if (ev.type == SDL_EVENT_TEXT_INPUT) {
                    const std::size_t limit = settings_sel == SettingSeed ? 20u : 10u;
                    bool has_digit = false;
                    for (const char* c = ev.text.text; *c; ++c) if (*c >= '0' && *c <= '9') { has_digit = true; break; }
                    if (has_digit && settings_number_replace_on_type) { settings_number_text.clear(); settings_number_replace_on_type = false; }
                    for (const char* c = ev.text.text; *c && settings_number_text.size() < limit; ++c) {
                        if (*c >= '0' && *c <= '9') settings_number_text.push_back(*c);
                    }
                } else if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                    if (ev.key.key == SDLK_BACKSPACE) {
                        if (settings_number_replace_on_type) { settings_number_text.clear(); settings_number_replace_on_type = false; }
                        else if (!settings_number_text.empty()) settings_number_text.pop_back();
                    } else if (ev.key.key == SDLK_ESCAPE) {
                        cancelSettingNumberEdit();
                    } else if (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER) {
                        applySettingNumberEdit();
                    }
                }
                return SDL_APP_CONTINUE;
            }

            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                const auto key = ev.key.key;
                if (key == SDLK_ESCAPE) {
                    saveConfig(config_path, cfg);
                    settings_status.clear();
                    screen = Screen::Menu;
                } else if (key == SDLK_UP) {
                    settings_sel = (settings_sel + SettingCount - 1) % SettingCount;
                    settings_status.clear();
                } else if (key == SDLK_DOWN) {
                    settings_sel = (settings_sel + 1) % SettingCount;
                    settings_status.clear();
                } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                    activateSetting();
                } else if (key == SDLK_LEFT || key == SDLK_RIGHT) {
                    adjustSetting(key == SDLK_RIGHT ? 1 : -1);
                }
            }
            return SDL_APP_CONTINUE;
        }

        if (screen == Screen::Miscellaneous) {
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.key == SDLK_ESCAPE) {
                screen = Screen::Settings;
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
                    screen = Screen::Settings;
                    saveConfig(config_path, cfg);
                } else if (key == SDLK_UP) {
                    controls_sel = (controls_sel + kControlItemCount - 1) % kControlItemCount;
                } else if (key == SDLK_DOWN) {
                    controls_sel = (controls_sel + 1) % kControlItemCount;
                } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                    if (controls_sel == kControlResetIndex) {
                        resetControls(cfg);
                        saveConfig(config_path, cfg);
                    } else {
                        rebinding = true;
                        wait_pad = false;
                    }
                } else if (key == SDLK_G && controls_sel < kControlResetIndex) {
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
                else if (key == SDLK_F6) { viewer.paused = true; openReplaySaveDialog(viewer.rep, ev.key.timestamp, false); }
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
                    openReplaySaveDialog(run.snapshot(), timestamp, true);
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
        processReplayDialog();

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
            info.show_inputs = cfg.show_inputs;
            renderGame(ren, *run.game, info);
        } else if (screen == Screen::Replay && viewer.game) {
            viewer.tick(now);
            RenderInfo info;
            info.seed = viewer.rep.seed;
            info.replay_mode = true;
            info.replay_speed = viewer.speed;
            info.replay_paused = viewer.paused;
            info.recent_inputs = viewer.recent();
            info.show_inputs = cfg.show_inputs;
            info.status = !replay_status.empty()
                              ? replay_status
                              : (viewer.rep.final_hash.empty()
                                  ? "UNVERIFIED REPLAY"
                                  : (verifyReplay(viewer.rep) ? "REPLAY VERIFIED" : "REPLAY HASH FAILED"));
            renderGame(ren, *viewer.game, info);
        } else if (screen == Screen::Menu) {
            renderMenu(ren, menu_sel, cfg.rules.tournament);
        } else if (screen == Screen::ReplayMenu) {
            renderReplayMenu(ren, replay_menu_sel, lastReplayExists(), replay_status);
        } else if (screen == Screen::Settings) {
            renderSettings(ren, cfg, seed, settings_sel, settings_number_editing, settings_number_text, settings_status);
        } else if (screen == Screen::SandboxSetup) {
            renderSandboxSetup(ren, cfg, custom_sel);
        } else if (screen == Screen::Controls) {
            renderControls(ren, cfg, controls_sel, rebinding, wait_pad);
        } else if (screen == Screen::Miscellaneous) {
            renderMiscellaneous(ren);
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
