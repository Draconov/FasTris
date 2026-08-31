#include "app_config.hpp"
#include "renderer.hpp"
#include "fasttris/game.hpp"
#include "fasttris/replay.hpp"
#include "fasttris/seed.hpp"
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
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <memory>
#include <limits>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

using namespace fasttris;
using namespace fasttris::app;

namespace {
enum class Screen { Menu, Game, Settings, SeedSettings, Controls, Miscellaneous, Shaders, Textures, Palettes, Help, Replay, ReplayMenu, SandboxSetup };

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
#if defined(__EMSCRIPTEN__)
struct WebReplayBuffer {
    unsigned char* data{};
    std::size_t size{};
    WebReplayBuffer()=default;
    WebReplayBuffer(unsigned char* p,std::size_t n):data(p),size(n){}
    WebReplayBuffer(const WebReplayBuffer&)=delete;
    WebReplayBuffer& operator=(const WebReplayBuffer&)=delete;
    WebReplayBuffer(WebReplayBuffer&& other) noexcept : data(other.data),size(other.size){other.data=nullptr;other.size=0;}
    WebReplayBuffer& operator=(WebReplayBuffer&& other) noexcept {
        if(this!=&other){if(data)std::free(data);data=other.data;size=other.size;other.data=nullptr;other.size=0;}
        return *this;
    }
    ~WebReplayBuffer(){if(data)std::free(data);}
    std::span<const std::uint8_t> bytes() const { return {reinterpret_cast<const std::uint8_t*>(data),size}; }
};
#endif
struct ReplayDialogResult {
    ReplayDialogAction action{ReplayDialogAction::Load};
    std::string path;
#if defined(__EMSCRIPTEN__)
    std::optional<WebReplayBuffer> web_buffer;
#endif
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

enum class SeedClipboardAction { Copy, Paste };
struct SeedClipboardResult {
    SeedClipboardAction action{SeedClipboardAction::Paste};
    std::string text;
    std::string error;
};
struct SeedClipboardMailbox {
    std::mutex mutex;
    std::optional<SeedClipboardResult> pending;
};

#if defined(__EMSCRIPTEN__)
std::weak_ptr<ReplayDialogMailbox> g_web_replay_mailbox;
std::weak_ptr<SeedClipboardMailbox> g_web_seed_clipboard_mailbox;

void postWebReplayResult(ReplayDialogResult result) {
    if (auto mailbox = g_web_replay_mailbox.lock()) {
        std::lock_guard<std::mutex> lock(mailbox->mutex);
        mailbox->pending = std::move(result);
    }
}

void postWebSeedClipboardResult(SeedClipboardResult result) {
    if (auto mailbox = g_web_seed_clipboard_mailbox.lock()) {
        std::lock_guard<std::mutex> lock(mailbox->mutex);
        mailbox->pending = std::move(result);
    }
}

EM_JS(void, webChooseReplayFile, (), {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.ftr,application/octet-stream';
    input.style.display = 'none';
    let settled = false;

    const cleanup = () => {
        window.removeEventListener('focus', onWindowFocus);
        input.remove();
    };
    const cancel = () => {
        if (settled) return;
        settled = true;
        cleanup();
        Module.ccall('fastris_web_replay_canceled', null, [], []);
    };
    const onWindowFocus = () => {
        setTimeout(() => {
            if (!settled && (!input.files || input.files.length === 0)) cancel();
        }, 350);
    };

    input.addEventListener('change', () => {
        if (settled) return;
        const file = input.files && input.files[0];
        if (!file) { cancel(); return; }
        settled = true;
        window.removeEventListener('focus', onWindowFocus);

        if (file.size > 16 * 1024 * 1024) {
            cleanup();
            Module.ccall('fastris_web_replay_error', null, ['string'], ['replay file is too large']);
            return;
        }

        const reader = new FileReader();
        reader.onload = () => {
            const bytes = reader.result instanceof ArrayBuffer ? new Uint8Array(reader.result) : new Uint8Array();
            cleanup();
            const ptr = Module._malloc(Math.max(1, bytes.length));
            if (!ptr) {
                Module.ccall('fastris_web_replay_error', null, ['string'], ['not enough memory to load replay']);
                return;
            }
            if (bytes.length) HEAPU8.set(bytes, ptr);
            // Ownership transfers to C++; it frees the WASM allocation after
            // incremental decoding completes or the load is canceled/failed.
            Module.ccall('fastris_web_replay_loaded_bytes', null, ['number','number'], [ptr, bytes.length]);
        };
        reader.onerror = () => {
            cleanup();
            Module.ccall('fastris_web_replay_error', null, ['string'], ['browser could not read replay file']);
        };
        reader.readAsArrayBuffer(file);
    });

    document.body.appendChild(input);
    window.addEventListener('focus', onWindowFocus);
    input.click();
});

EM_JS(void, webDownloadReplayFile, (const char* filename, const void* contents, int size), {
    const name = UTF8ToString(filename);
    const bytes = HEAPU8.slice(contents, contents + size);
    const blob = new Blob([bytes], {type: 'application/octet-stream'});
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = name;
    anchor.style.display = 'none';
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
});

EM_JS(void, webCopySeedToClipboard, (const char* contents), {
    const text=UTF8ToString(contents);
    const ok=()=>Module.ccall('fastris_web_seed_copy_ok',null,[],[]);
    const fail=(message)=>Module.ccall('fastris_web_seed_clipboard_error',null,['string'],[message||'browser clipboard copy failed']);
    const fallback=()=>{
        try{
            const area=document.createElement('textarea');
            area.value=text;
            area.setAttribute('readonly','');
            area.style.position='fixed';
            area.style.opacity='0';
            document.body.appendChild(area);
            area.select();
            const copied=document.execCommand('copy');
            area.remove();
            copied?ok():fail('browser blocked clipboard copy');
        }catch(e){fail('browser blocked clipboard copy');}
    };
    if(navigator.clipboard&&navigator.clipboard.writeText&&window.isSecureContext){
        navigator.clipboard.writeText(text).then(ok).catch(()=>fallback());
    }else fallback();
});

EM_JS(void, webPasteSeedFromClipboard, (), {
    const fail=(message)=>Module.ccall('fastris_web_seed_clipboard_error',null,['string'],[message||'browser clipboard paste failed']);
    if(navigator.clipboard&&navigator.clipboard.readText&&window.isSecureContext){
        navigator.clipboard.readText().then((text)=>{
            Module.ccall('fastris_web_seed_pasted',null,['string'],[text]);
        }).catch(()=>fail('browser blocked clipboard read'));
    }else fail('clipboard read is unavailable in this browser');
});
#endif

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
    if(size>kMaxReplayBytes){SDL_free(raw);err="replay file is too large";return false;}
    const bool ok=deserializeReplay(std::string_view(static_cast<const char*>(raw),size),replay,&err);
    SDL_free(raw);
    return ok;
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

    // One incremental pass performs load-time verification and builds seek /
    // analysis indexes. Playback reuses those results instead of re-simulating
    // work independently.
    std::unique_ptr<ReplayIndexBuilder> index_builder;
    std::optional<bool> end_verification_result;

    bool load(const std::string& path, std::string& err) {
        if (!loadReplay(path, rep, &err)) return false;
        beginIndexing();
        reset(0);
        return true;
    }

    void load(Replay replay) {
        rep = std::move(replay);
        beginIndexing();
        reset(0);
    }

    void beginIndexing() {
        // A newly loaded replay always starts as a fresh viewer session.
        // Do not inherit pause/speed state from a replay that was watched
        // previously (for example one that auto-paused at its end).
        paused = false;
        speed = 1.0;
        end_verification_result.reset();
        index_builder = std::make_unique<ReplayIndexBuilder>(rep);
    }

    const ReplayIndex* replayIndex() const {
        return index_builder ? &index_builder->index() : nullptr;
    }

    void reset(TimeUs target) {
        game = std::make_unique<Game>(rep.seed, rep.mode, rep.rules);
        index = 0;
        playhead = 0;
        seek(target);
        // Let the next tick establish the wall-clock baseline. reset() can be
        // called after the app frame timestamp has already been sampled (for
        // example after a Web file-picker callback). Sampling a newer clock
        // value here and then subtracting it from that older frame timestamp
        // would underflow Uint64 and look like an enormous playback delta.
        last_wall_ns = 0;
    }

    const ReplayCheckpoint* bestCheckpoint(TimeUs target) const {
        const auto* ri=replayIndex();
        if(!ri||ri->checkpoints.empty())return nullptr;
        const auto it=std::upper_bound(ri->checkpoints.begin(),ri->checkpoints.end(),target,
            [](TimeUs t,const ReplayCheckpoint& cp){return t<cp.time_us;});
        if(it==ri->checkpoints.begin())return nullptr;
        return &*std::prev(it);
    }

    void restoreCheckpoint(const ReplayCheckpoint& cp) {
        if(game)*game=cp.game;else game=std::make_unique<Game>(cp.game);
        index=cp.event_index;
        playhead=cp.time_us;
    }

    void seek(TimeUs target) {
        target = std::clamp<TimeUs>(target, 0, rep.duration_us);
        if(!game){game=std::make_unique<Game>(rep.seed,rep.mode,rep.rules);index=0;playhead=0;}

        const bool backward=target<playhead;
        const TimeUs checkpoint_interval=(replayIndex()?replayIndex()->checkpoint_interval_us:kReplayCheckpointBaseIntervalUs);
        const bool long_forward=target>playhead+checkpoint_interval*2;
        if(backward||long_forward){
            if(const auto* cp=bestCheckpoint(target);cp&&(backward||cp->time_us>playhead)){
                restoreCheckpoint(*cp);
            }else if(backward){
                game=std::make_unique<Game>(rep.seed,rep.mode,rep.rules);
                index=0;playhead=0;
            }
        }

        while(index<rep.events.size()&&rep.events[index].time_us<=target){
            const auto& e=rep.events[index];
            game->advanceTo(e.time_us);
            if(e.down)game->press(e.action);else game->release(e.action);
            ++index;
        }
        game->advanceTo(target);
        playhead=target;
    }

    void stepLoadVerification() {
        if(!index_builder||index_builder->finished())return;
        constexpr Uint64 kWallBudgetNs=1'000'000ULL; // <= ~1 ms per frame.
        constexpr int kMaxStepsPerFrame=64;
        const Uint64 start=SDL_GetTicksNS();
        for(int steps=0;steps<kMaxStepsPerFrame&&!index_builder->finished();++steps){
            if(!index_builder->step())break;
            // Keep a hard step cap as well as the wall-clock budget. The cap
            // guarantees yielding even on platforms where timer resolution is
            // coarse or unusual inside a tight WebAssembly loop.
            if(SDL_GetTicksNS()-start>=kWallBudgetNs)break;
        }
    }

    void verifyEndStateIfNeeded() {
        if(playhead<rep.duration_us||end_verification_result.has_value()||!rep.final_hash.has_value())return;
        end_verification_result=stateHash(*game)==*rep.final_hash;
    }

    std::optional<bool> visibleVerificationResult() const {
        const auto load_result=(index_builder?index_builder->index().verification:std::optional<bool>{});
        if((load_result.has_value()&&!*load_result)||(end_verification_result.has_value()&&!*end_verification_result))return false;
        if(end_verification_result.has_value())return end_verification_result;
        if(load_result.has_value())return load_result;
        return std::nullopt;
    }

    void tick(Uint64 now) {
        stepLoadVerification();

        // A replay may be loaded from an asynchronous browser/native dialog
        // after this frame's `now` value was sampled. Never subtract an older
        // timestamp from a newer baseline: Uint64 underflow would turn one
        // frame into a gigantic seek and can lock up the Web main thread.
        if(last_wall_ns==0 || now<=last_wall_ns){
            last_wall_ns=now;
            verifyEndStateIfNeeded();
            return;
        }

        if(!paused){
            // Do not catch up an arbitrarily long browser/tab stall in one
            // frame. Replay playback is presentation time, so after a stall we
            // resume smoothly instead of synchronously simulating seconds (or
            // minutes) of replay on the UI thread.
            constexpr Uint64 kMaxPlaybackWallDeltaNs=100'000'000ULL; // 100 ms
            const Uint64 delta=std::min(now-last_wall_ns,kMaxPlaybackWallDeltaNs);
            const auto add=static_cast<TimeUs>((delta/1000.0)*speed);
            seek(std::min(rep.duration_us,playhead+add));
            if(playhead>=rep.duration_us)paused=true;
        }
        verifyEndStateIfNeeded();
        last_wall_ns=now;
    }

    std::span<const ReplayEvent> recent() const {
        const std::size_t end=std::min(index,rep.events.size());
        const std::size_t begin=end>8?end-8:0;
        return std::span<const ReplayEvent>(rep.events.data()+begin,end-begin);
    }

    void nextPiece() {
        if(const auto* ri=replayIndex();ri&&!ri->pieces.empty()){
            const auto it=std::upper_bound(ri->pieces.begin(),ri->pieces.end(),playhead,
                [](TimeUs t,const ReplayMarker& marker){return t<marker.time_us;});
            if(it!=ri->pieces.end()){seek(it->time_us);return;}
            if(index_builder&&index_builder->finished())seek(rep.duration_us);
        }
        // While incremental indexing is still catching up, do nothing rather
        // than maintaining a second redundant HardDrop-only piece index.
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
        if (game && !paused) { game->advanceTo(simAt(ns)); replay.final_hash.reset(); }
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
        replay.final_hash.reset();
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

    const Replay& prepareReplay() {
        if(game){replay.duration_us=game->now();replay.final_hash=stateHash(*game);}
        return replay;
    }

    bool save(const std::string& path) {
        if (!game) return false;
        prepareReplay();
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
    int seed_settings_sel{};
    int controls_sel{};
    int misc_sel{kMiscShadersIndex};
    int shader_sel{};
    int texture_sel{};
    int palette_sel{};
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
    bool seed_number_editing{};
    bool seed_number_replace_on_type{};
    std::string seed_number_text;
    std::string seed_status;
    std::string config_path;
    std::string last_replay_path;
    std::string replay_status;
    std::shared_ptr<ReplayDialogMailbox> replay_dialog_mailbox{std::make_shared<ReplayDialogMailbox>()};
    std::shared_ptr<SeedClipboardMailbox> seed_clipboard_mailbox{std::make_shared<SeedClipboardMailbox>()};
    bool replay_dialog_open{};
    bool replay_dialog_paused_run{};
    bool pending_save_from_game{};
    const Replay* pending_replay_save{};
#if defined(__EMSCRIPTEN__)
    std::optional<WebReplayBuffer> web_replay_buffer;
    std::unique_ptr<ReplayDecoder> web_replay_decoder;
#endif
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
#if defined(__EMSCRIPTEN__)
        g_web_replay_mailbox=replay_dialog_mailbox;
        webChooseReplayFile();
#else
        auto* context=new ReplayDialogContext{replay_dialog_mailbox,ReplayDialogAction::Load};
        SDL_ShowOpenFileDialog(replayDialogCallback,context,win,kReplayFilters,1,nullptr,false);
#endif
    }

    void openReplaySaveDialog(const Replay& replay,Uint64 now,bool from_game) {
        if(replay_dialog_open)return;
        if(from_game&&run.game&&run.game->rules().tournament&&!run.game->complete()&&!run.game->gameOver()){
            run.status="SAVE FILE DISABLED DURING TOURNAMENT RUN";
            return;
        }
#if defined(__EMSCRIPTEN__)
        const std::string bytes=serializeReplay(replay);
        if(bytes.empty()){
            if(from_game)run.status="REPLAY ENCODE FAILED";else replay_status="REPLAY ENCODE FAILED";
            return;
        }
        const std::string filename="FasTris-Replay-"+std::to_string(replay.seed)+".ftr";
        webDownloadReplayFile(filename.c_str(),bytes.data(),static_cast<int>(bytes.size()));
        if(from_game)run.status="REPLAY DOWNLOAD STARTED";
        else replay_status="REPLAY DOWNLOAD STARTED";
        (void)now;
        return;
#else
        pending_replay_save=&replay;
        pending_save_from_game=from_game;
        replay_dialog_open=true;
        replay_dialog_paused_run=false;
        if(from_game&&run.game&&!run.paused){
            run.togglePause(now);
            replay_dialog_paused_run=true;
        }
        if(from_game)run.status="CHOOSE REPLAY FILE";
        else replay_status="CHOOSE REPLAY FILE";
        auto* context=new ReplayDialogContext{replay_dialog_mailbox,ReplayDialogAction::Save};
        SDL_ShowSaveFileDialog(replayDialogCallback,context,win,kReplayFilters,1,nullptr);
#endif
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
#if defined(__EMSCRIPTEN__)
            if(result->web_buffer){
                web_replay_buffer=std::move(*result->web_buffer);
                web_replay_decoder=std::make_unique<ReplayDecoder>(web_replay_buffer->bytes());
                replay_status="LOADING REPLAY";
                return;
            }
#endif
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
        pending_replay_save=nullptr;
        pending_save_from_game=false;
        if(resume_run&&run.game&&run.paused)run.togglePause(SDL_GetTicksNS());
    }

#if defined(__EMSCRIPTEN__)
    void processWebReplayDecode() {
        if(!web_replay_decoder)return;
        constexpr Uint64 kDecodeBudgetNs=1'000'000ULL;
        constexpr std::size_t kEventsPerStep=2048u;
        constexpr int kMaxStepsPerFrame=8;
        const Uint64 start=SDL_GetTicksNS();
        for(int step=0;step<kMaxStepsPerFrame&&!web_replay_decoder->finished();++step){
            web_replay_decoder->step(kEventsPerStep);
            if(SDL_GetTicksNS()-start>=kDecodeBudgetNs)break;
        }
        if(!web_replay_decoder->finished()){
            const auto total=web_replay_decoder->expectedEvents();
            const auto done=web_replay_decoder->decodedEvents();
            if(total>0)replay_status="LOADING REPLAY "+std::to_string((done*100u)/total)+"%";
            return;
        }
        if(!web_replay_decoder->ok()){
            replay_status="LOAD FAILED: "+web_replay_decoder->error();
            web_replay_decoder.reset();
            web_replay_buffer.reset();
            return;
        }
        Replay loaded=web_replay_decoder->takeReplay();
        web_replay_decoder.reset();
        web_replay_buffer.reset();
        viewer.load(std::move(loaded));
        replay_status.clear();
        screen=Screen::Replay;
    }
#endif

    void processSeedClipboard() {
        std::optional<SeedClipboardResult> result;
        {
            std::lock_guard<std::mutex> lock(seed_clipboard_mailbox->mutex);
            if(seed_clipboard_mailbox->pending){
                result=std::move(seed_clipboard_mailbox->pending);
                seed_clipboard_mailbox->pending.reset();
            }
        }
        if(!result)return;
        if(!result->error.empty()){
            seed_status=result->error;
            return;
        }
        if(result->action==SeedClipboardAction::Copy){
            seed_status="SEED COPIED";
            return;
        }
        std::uint64_t value=0;
        if(!firstSeedInText(result->text,value)){
            seed_status="CLIPBOARD DOES NOT CONTAIN A VALID UINT64 SEED";
            return;
        }
        seed=value;
        seed_status="SEED PASTED";
    }

    void copySeed() {
        const std::string text=std::to_string(seed);
#if defined(__EMSCRIPTEN__)
        g_web_seed_clipboard_mailbox=seed_clipboard_mailbox;
        seed_status="COPYING SEED...";
        webCopySeedToClipboard(text.c_str());
#else
        if(SDL_SetClipboardText(text.c_str())) seed_status="SEED COPIED";
        else seed_status=std::string("COPY FAILED: ")+SDL_GetError();
#endif
    }

    void pasteSeed() {
#if defined(__EMSCRIPTEN__)
        g_web_seed_clipboard_mailbox=seed_clipboard_mailbox;
        seed_status="READING CLIPBOARD...";
        webPasteSeedFromClipboard();
#else
        char* raw=SDL_GetClipboardText();
        if(!raw){seed_status=std::string("PASTE FAILED: ")+SDL_GetError();return;}
        std::string text(raw);
        SDL_free(raw);
        std::uint64_t value=0;
        if(!firstSeedInText(text,value)){
            seed_status="CLIPBOARD DOES NOT CONTAIN A VALID UINT64 SEED";
            return;
        }
        seed=value;
        seed_status="SEED PASTED";
#endif
    }

    bool settingLocked(int item) const {
        return cfg.rules.tournament && item >= SettingLock && item <= SettingNext;
    }

    static bool numericSetting(int item) {
        switch (item) {
            case SettingDas: case SettingArr: case SettingSdf: case SettingDcd:
            case SettingLock: case SettingResets: case SettingNext:
            case SettingFpsCap:
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
                default: break;
            }
            if (!ok) return false;
        } catch (...) {
            settings_status = "INVALID NUMBER";
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

    void beginSeedNumberEdit() {
        seed_number_text=std::to_string(seed);
        seed_status.clear();
        seed_number_editing=true;
        seed_number_replace_on_type=true;
        SDL_StartTextInput(win);
    }

    void cancelSeedNumberEdit() {
        if(!seed_number_editing)return;
        seed_number_editing=false;
        seed_number_replace_on_type=false;
        seed_number_text.clear();
        seed_status.clear();
        SDL_StopTextInput(win);
    }

    bool applySeedNumberEdit() {
        std::uint64_t value=0;
        if(!firstSeedInText(seed_number_text,value)){
            seed_status="INVALID UINT64 SEED";
            return false;
        }
        seed=value;
        seed_number_editing=false;
        seed_number_replace_on_type=false;
        seed_number_text.clear();
        seed_status="SEED APPLIED";
        SDL_StopTextInput(win);
        return true;
    }

    static int controlGridIndex(int row,int col) {
        static constexpr int grid[3][4]={
            {0,1,2,3},
            {4,5,6,7},
            {8,9,kControlResetIndex,-1}
        };
        row=std::clamp(row,0,2);
        col=std::clamp(col,0,3);
        if(grid[row][col]>=0)return grid[row][col];
        for(int c=col-1;c>=0;--c)if(grid[row][c]>=0)return grid[row][c];
        return grid[row][0];
    }

    static std::pair<int,int> controlGridPosition(int index) {
        static constexpr int grid[3][4]={
            {0,1,2,3},
            {4,5,6,7},
            {8,9,kControlResetIndex,-1}
        };
        for(int row=0;row<3;++row)for(int col=0;col<4;++col)if(grid[row][col]==index)return {row,col};
        return {0,0};
    }

    void moveControlSelection(int dx,int dy) {
        auto [row,col]=controlGridPosition(controls_sel);
        if(dy!=0){
            row=(row+dy+3)%3;
            controls_sel=controlGridIndex(row,col);
            return;
        }
        if(dx!=0){
            int next=col;
            for(int tries=0;tries<4;++tries){
                next=(next+dx+4)%4;
                const int candidate=controlGridIndex(row,next);
                const auto [candidateRow,candidateCol]=controlGridPosition(candidate);
                if(candidateRow==row&&candidateCol==next){controls_sel=candidate;return;}
            }
        }
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
            default: return;
        }
        settings_status.clear();
        saveConfig(config_path, cfg);
    }

    int shaderSettingsItemCount() const {
        return static_cast<int>(shaderControls(cfg.shader).size()) + 2; // shader + controls + back
    }

    int shaderSettingsBackIndex() const {
        return shaderSettingsItemCount() - 1;
    }

    void cycleShader(int delta) {
        int value=static_cast<int>(cfg.shader);
        value=(value+(delta>0?1:-1)+kVisualShaderCount)%kVisualShaderCount;
        cfg.shader=static_cast<VisualShader>(value);
        shader_sel=0;
        saveConfig(config_path,cfg);
    }

    void adjustShaderSetting(int delta) {
        if(delta==0)return;
        if(shader_sel==0){
            cycleShader(delta);
            return;
        }
        const auto controls=shaderControls(cfg.shader);
        const int control_index=shader_sel-1;
        if(control_index<0||control_index>=static_cast<int>(controls.size()))return;
        adjustShaderControl(cfg,controls[static_cast<std::size_t>(control_index)],delta);
        saveConfig(config_path,cfg);
    }

    int textureSettingsItemCount() const {
        return static_cast<int>(textureControls(cfg.texture).size()) + 2; // texture + controls + back
    }

    int textureSettingsBackIndex() const {
        return textureSettingsItemCount() - 1;
    }

    void cycleTexture(int delta) {
        int value=static_cast<int>(cfg.texture);
        value=(value+(delta>0?1:-1)+kVisualTextureCount)%kVisualTextureCount;
        cfg.texture=static_cast<VisualTexture>(value);
        texture_sel=0;
        saveConfig(config_path,cfg);
    }

    void adjustTextureSetting(int delta) {
        if(delta==0)return;
        if(texture_sel==0){
            cycleTexture(delta);
            return;
        }
        const auto controls=textureControls(cfg.texture);
        const int control_index=texture_sel-1;
        if(control_index<0||control_index>=static_cast<int>(controls.size()))return;
        adjustTextureControl(cfg,controls[static_cast<std::size_t>(control_index)],delta);
        saveConfig(config_path,cfg);
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
            case SettingSeedMenu:
                seed_settings_sel=SeedSettingValue;
                seed_status.clear();
                screen=Screen::SeedSettings;
                break;
            case SettingControls:
                controls_sel = 0;
                settings_status.clear();
                screen = Screen::Controls;
                break;
            case SettingMiscellaneous:
                settings_status.clear();
                misc_sel=kMiscShadersIndex;
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
            Sha256Digest actual{};
            if (!loadReplay(verify_path, replay, &err)) {
                std::cerr << "Replay load failed: " << err << "\n";
                return false;
            }
            const bool ok = verifyReplay(replay, &actual);
            std::cout << (ok ? "VERIFIED\n" : "FAILED\n")
                      << "expected: " << (replay.final_hash?hexLower(*replay.final_hash):std::string("<missing>")) << "\n"
                      << "actual:   " << hexLower(actual) << "\n";
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
                    constexpr std::size_t limit = 10u;
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

        if (screen == Screen::SeedSettings) {
            if(seed_number_editing){
                if(ev.type==SDL_EVENT_TEXT_INPUT){
                    constexpr std::size_t limit=20u;
                    bool has_digit=false;
                    for(const char* c=ev.text.text;*c;++c)if(*c>='0'&&*c<='9'){has_digit=true;break;}
                    if(has_digit&&seed_number_replace_on_type){seed_number_text.clear();seed_number_replace_on_type=false;}
                    for(const char* c=ev.text.text;*c&&seed_number_text.size()<limit;++c){
                        if(*c>='0'&&*c<='9')seed_number_text.push_back(*c);
                    }
                }else if(ev.type==SDL_EVENT_KEY_DOWN&&!ev.key.repeat){
                    if(ev.key.key==SDLK_BACKSPACE){
                        if(seed_number_replace_on_type){seed_number_text.clear();seed_number_replace_on_type=false;}
                        else if(!seed_number_text.empty())seed_number_text.pop_back();
                    }else if(ev.key.key==SDLK_ESCAPE)cancelSeedNumberEdit();
                    else if(ev.key.key==SDLK_RETURN||ev.key.key==SDLK_KP_ENTER)applySeedNumberEdit();
                }
                return SDL_APP_CONTINUE;
            }

            if(ev.type==SDL_EVENT_KEY_DOWN&&!ev.key.repeat){
                const auto key=ev.key.key;
                const bool clipboard_modifier=(ev.key.mod&(SDL_KMOD_CTRL|SDL_KMOD_GUI))!=0;
                if(clipboard_modifier&&key==SDLK_C){
                    copySeed();
                }else if(clipboard_modifier&&key==SDLK_V){
                    pasteSeed();
                }else if(key==SDLK_ESCAPE){
                    seed_status.clear();
                    screen=Screen::Settings;
                }else if(key==SDLK_UP){
                    seed_settings_sel=(seed_settings_sel+SeedSettingCount-1)%SeedSettingCount;
                    seed_status.clear();
                }else if(key==SDLK_DOWN){
                    seed_settings_sel=(seed_settings_sel+1)%SeedSettingCount;
                    seed_status.clear();
                }else if(key==SDLK_RETURN||key==SDLK_KP_ENTER){
                    switch(seed_settings_sel){
                        case SeedSettingValue: beginSeedNumberEdit(); break;
                        case SeedSettingRandomize: seed=randomSeed();seed_status="NEW RANDOM SEED";break;
                        case SeedSettingCopy: copySeed(); break;
                        case SeedSettingPaste: pasteSeed(); break;
                        case SeedSettingBack: seed_status.clear();screen=Screen::Settings;break;
                        default: break;
                    }
                }
            }
            return SDL_APP_CONTINUE;
        }

        if (screen == Screen::Miscellaneous) {
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                const auto key=ev.key.key;
                if(key==SDLK_ESCAPE){
                    screen=Screen::Settings;
                }else if(key==SDLK_UP){
                    misc_sel=(misc_sel+kMiscItemCount-1)%kMiscItemCount;
                }else if(key==SDLK_DOWN){
                    misc_sel=(misc_sel+1)%kMiscItemCount;
                }else if((key==SDLK_RETURN||key==SDLK_KP_ENTER)&&misc_sel==kMiscShadersIndex){
                    shader_sel=0;
                    screen=Screen::Shaders;
                }else if((key==SDLK_RETURN||key==SDLK_KP_ENTER)&&misc_sel==kMiscTexturesIndex){
                    texture_sel=0;
                    screen=Screen::Textures;
                }else if((key==SDLK_RETURN||key==SDLK_KP_ENTER)&&misc_sel==kMiscPalettesIndex){
                    palette_sel=static_cast<int>(cfg.palette);
                    screen=Screen::Palettes;
                }
            }
            return SDL_APP_CONTINUE;
        }

        if(screen==Screen::Shaders){
            if(ev.type==SDL_EVENT_KEY_DOWN&&!ev.key.repeat){
                const auto key=ev.key.key;
                const int item_count=shaderSettingsItemCount();
                if(key==SDLK_ESCAPE){
                    screen=Screen::Miscellaneous;
                }else if(key==SDLK_UP){
                    shader_sel=(shader_sel+item_count-1)%item_count;
                }else if(key==SDLK_DOWN){
                    shader_sel=(shader_sel+1)%item_count;
                }else if(key==SDLK_LEFT){
                    adjustShaderSetting(-1);
                }else if(key==SDLK_RIGHT){
                    adjustShaderSetting(1);
                }else if(key==SDLK_RETURN||key==SDLK_KP_ENTER){
                    if(shader_sel==shaderSettingsBackIndex())screen=Screen::Miscellaneous;
                    else if(shader_sel==0)cycleShader(1);
                }
            }
            return SDL_APP_CONTINUE;
        }

        if(screen==Screen::Textures){
            if(ev.type==SDL_EVENT_KEY_DOWN&&!ev.key.repeat){
                const auto key=ev.key.key;
                const int item_count=textureSettingsItemCount();
                if(key==SDLK_ESCAPE){
                    screen=Screen::Miscellaneous;
                }else if(key==SDLK_UP){
                    texture_sel=(texture_sel+item_count-1)%item_count;
                }else if(key==SDLK_DOWN){
                    texture_sel=(texture_sel+1)%item_count;
                }else if(key==SDLK_LEFT){
                    adjustTextureSetting(-1);
                }else if(key==SDLK_RIGHT){
                    adjustTextureSetting(1);
                }else if(key==SDLK_RETURN||key==SDLK_KP_ENTER){
                    if(texture_sel==textureSettingsBackIndex())screen=Screen::Miscellaneous;
                    else if(texture_sel==0)cycleTexture(1);
                }
            }
            return SDL_APP_CONTINUE;
        }

        if(screen==Screen::Palettes){
            if(ev.type==SDL_EVENT_KEY_DOWN&&!ev.key.repeat){
                const auto key=ev.key.key;
                if(key==SDLK_ESCAPE||key==SDLK_RETURN||key==SDLK_KP_ENTER){
                    screen=Screen::Miscellaneous;
                }else if(key==SDLK_UP||key==SDLK_LEFT){
                    palette_sel=(palette_sel+kVisualPaletteCount-1)%kVisualPaletteCount;
                    cfg.palette=static_cast<VisualPalette>(palette_sel);
                    saveConfig(config_path,cfg);
                }else if(key==SDLK_DOWN||key==SDLK_RIGHT){
                    palette_sel=(palette_sel+1)%kVisualPaletteCount;
                    cfg.palette=static_cast<VisualPalette>(palette_sel);
                    saveConfig(config_path,cfg);
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
                    screen = Screen::Settings;
                    saveConfig(config_path, cfg);
                } else if (key == SDLK_UP) {
                    moveControlSelection(0,-1);
                } else if (key == SDLK_DOWN) {
                    moveControlSelection(0,1);
                } else if (key == SDLK_LEFT) {
                    moveControlSelection(-1,0);
                } else if (key == SDLK_RIGHT) {
                    moveControlSelection(1,0);
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
                    openReplaySaveDialog(run.prepareReplay(), timestamp, true);
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
#if defined(__EMSCRIPTEN__)
        processWebReplayDecode();
#endif
        processSeedClipboard();
        setVisualPalette(cfg.palette);
        setVisualTexture(cfg);
        setVisualShader(cfg);
        beginVisualShaderFrame(ren);

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
            info.recent_inputs = std::span<const ReplayEvent>(run.recent);
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
            if (!replay_status.empty()) {
                info.status = replay_status;
            } else if (viewer.paused) {
                if (!viewer.rep.final_hash.has_value()) {
                    info.status = "UNVERIFIED REPLAY";
                } else if (const auto verified = viewer.visibleVerificationResult(); verified.has_value()) {
                    info.status = *verified ? "REPLAY VERIFIED" : "REPLAY HASH FAILED";
                }
            }
            renderGame(ren, *viewer.game, info);
        } else if (screen == Screen::Menu) {
            renderMenu(ren, menu_sel, cfg.rules.tournament);
        } else if (screen == Screen::ReplayMenu) {
            renderReplayMenu(ren, replay_menu_sel, lastReplayExists(), replay_status);
        } else if (screen == Screen::Settings) {
            renderSettings(ren, cfg, settings_sel, settings_number_editing, settings_number_text, settings_status);
        } else if (screen == Screen::SeedSettings) {
            renderSeedSettings(ren, seed, seed_settings_sel, seed_number_editing, seed_number_text, seed_status);
        } else if (screen == Screen::SandboxSetup) {
            renderSandboxSetup(ren, cfg, custom_sel);
        } else if (screen == Screen::Controls) {
            renderControls(ren, cfg, controls_sel, rebinding, wait_pad);
        } else if (screen == Screen::Miscellaneous) {
            renderMiscellaneous(ren, cfg, misc_sel);
        } else if (screen == Screen::Shaders) {
            renderShaderSettings(ren, cfg, shader_sel);
        } else if (screen == Screen::Textures) {
            renderTextureSettings(ren, cfg, texture_sel);
        } else if (screen == Screen::Palettes) {
            renderPaletteSettings(ren, cfg, palette_sel);
        } else if (screen == Screen::Help) {
            renderHelp(ren);
        }

        applyVisualShader(ren);
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
        shutdownVisualShaderPipeline();
        for (auto* pad : pads) SDL_CloseGamepad(pad);
        pads.clear();
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        ren = nullptr;
        win = nullptr;
    }
};
} // namespace

#if defined(__EMSCRIPTEN__)
extern "C" EMSCRIPTEN_KEEPALIVE void fastris_web_replay_loaded_bytes(unsigned char* data, int size) {
    ReplayDialogResult result;
    result.action=ReplayDialogAction::Load;
    if(data&&size>=0) result.web_buffer.emplace(data,static_cast<std::size_t>(size));
    else {
        if(data)std::free(data);
        result.error="browser returned an empty replay";
    }
    postWebReplayResult(std::move(result));
}

extern "C" EMSCRIPTEN_KEEPALIVE void fastris_web_replay_canceled() {
    ReplayDialogResult result;
    result.action=ReplayDialogAction::Load;
    result.canceled=true;
    postWebReplayResult(std::move(result));
}

extern "C" EMSCRIPTEN_KEEPALIVE void fastris_web_replay_error(const char* message) {
    ReplayDialogResult result;
    result.action=ReplayDialogAction::Load;
    result.error=message?message:"browser replay picker failed";
    postWebReplayResult(std::move(result));
}

extern "C" EMSCRIPTEN_KEEPALIVE void fastris_web_seed_copy_ok() {
    SeedClipboardResult result;
    result.action=SeedClipboardAction::Copy;
    postWebSeedClipboardResult(std::move(result));
}

extern "C" EMSCRIPTEN_KEEPALIVE void fastris_web_seed_pasted(const char* text) {
    SeedClipboardResult result;
    result.action=SeedClipboardAction::Paste;
    if(text)result.text=text;
    else result.error="browser returned an empty clipboard";
    postWebSeedClipboardResult(std::move(result));
}

extern "C" EMSCRIPTEN_KEEPALIVE void fastris_web_seed_clipboard_error(const char* message) {
    SeedClipboardResult result;
    result.action=SeedClipboardAction::Paste;
    result.error=message?message:"browser clipboard operation failed";
    postWebSeedClipboardResult(std::move(result));
}
#endif

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
