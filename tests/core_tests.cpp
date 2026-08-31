#include "fasttris/bag.hpp"
#include "fasttris/battle.hpp"
#include "fasttris/board.hpp"
#include "fasttris/game.hpp"
#include "fasttris/replay.hpp"
#include "fasttris/scoring.hpp"
#include "fasttris/seed.hpp"
#include "fasttris/sha256.hpp"
#include "fasttris/tetromino.hpp"
#include <algorithm>
#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

using namespace fasttris;


static void test_seed_text_scanning() {
    std::uint64_t value=0;
    assert(firstSeedInText("seed: 4956273544639070971",value));
    assert(value==4956273544639070971ULL);
    assert(firstSeedInText("bad 18446744073709551616 then 42",value)); // first overflows; second wins.
    assert(value==42);
    assert(firstSeedInText("a=7 b=99",value)&&value==7);
    assert(!firstSeedInText("no decimal seed here",value));
}
static void test_rng_and_bag() {
    Bag7 a(123456), b(123456), c(654321);
    std::vector<Piece> av,bv,cv;
    for(int i=0;i<28;++i){av.push_back(a.pop());bv.push_back(b.pop());cv.push_back(c.pop());}
    assert(av==bv); assert(av!=cv);
    for(int off=0;off<28;off+=7){std::set<Piece> s(av.begin()+off,av.begin()+off+7);assert(s.size()==7);}
}
static void test_shapes() {
    for(Piece p: {Piece::I,Piece::J,Piece::L,Piece::O,Piece::S,Piece::T,Piece::Z})
        for(int r=0;r<4;++r){auto& bs=blocks(p,static_cast<Rotation>(r));std::set<std::pair<int,int>> seen;for(auto q:bs)seen.insert({q.x,q.y});assert(seen.size()==4);}
}
static void test_board() {
    Board b; assert(b.perfectClear());
    for(int x=0;x<10;++x){ActivePiece a{Piece::O,Rotation::Spawn,x-1,22}; if(x%2==0 && !b.collides(a)) b.stamp(a);}
    Board g; for(int i=0;i<3;++i) assert(g.addGarbageLine(i)); assert(!g.perfectClear());
    assert(g.rowMask(kBoardH-1)!=0);
}
static void test_game_determinism() {
    Rules r; r.handling.das_ms=85; r.handling.arr_ms=0; r.handling.sdf=20;
    Game a(42,Mode::Sprint40,r), b(42,Mode::Sprint40,r);
    struct E{TimeUs t;Action a;bool d;};
    std::array<E,12> es{{
        {1000,Action::RotateCW,true},{2000,Action::Left,true},{70000,Action::Left,false},
        {71000,Action::HardDrop,true},{90000,Action::Hold,true},{100000,Action::Right,true},
        {190000,Action::Right,false},{200000,Action::HardDrop,true},{220000,Action::Rotate180,true},
        {225000,Action::HardDrop,true},{300000,Action::SoftDrop,true},{350000,Action::SoftDrop,false}
    }};
    for(auto&e:es){a.advanceTo(e.t);b.advanceTo(e.t);if(e.d){a.press(e.a);b.press(e.a);}else{a.release(e.a);b.release(e.a);}}
    a.advanceTo(500000);b.advanceTo(500000);assert(a.deterministicState()==b.deterministicState());assert(stateHash(a)==stateHash(b));
}
static void test_seed_difference() {
    Game a(1),b(2); auto an=a.next(14),bn=b.next(14);assert(an!=bn);
}
static void test_replay() {
    Replay r; r.seed=778899; r.mode=Mode::Sprint40; r.rules.handling.das_ms=100; r.rules.handling.arr_ms=0;
    r.events={{1000,Action::RotateCW,true},{2000,Action::HardDrop,true},{3000,Action::Left,true},{4000,Action::Left,false},{5000,Action::HardDrop,true}};
    r.duration_us=100000;
    Game g(r.seed,r.mode,r.rules);for(auto&e:r.events){g.advanceTo(e.time_us);if(e.down)g.press(e.action);else g.release(e.action);}g.advanceTo(r.duration_us);r.final_hash=stateHash(g);
    assert(verifyReplay(r));
    std::string err;
    const std::string encoded=serializeReplay(r);
    assert(!encoded.empty());
    assert(encoded.size()<160); // compact binary metadata + delta-coded events + raw hash.
    assert(encoded.size()>=8);
    assert(encoded.substr(0,7)=="FASTRIS");
    Replay memory_copy;
    assert(deserializeReplay(encoded,memory_copy,&err));
    assert(verifyReplay(memory_copy));
    assert(memory_copy.events==r.events);
    assert(memory_copy.seed==r.seed);
    assert(memory_copy.final_hash==r.final_hash);

    auto path=(std::filesystem::temp_directory_path()/"fasttris_test.ftr").string();
    assert(saveReplay(r,path,&err));
    {
        std::ifstream saved(path,std::ios::binary|std::ios::ate);
        assert(saved.tellg()==static_cast<std::streamoff>(encoded.size()));
    }
    Replay x;assert(loadReplay(path,x,&err));assert(verifyReplay(x));std::filesystem::remove(path);

    // Current-format-only policy: old textual replay data is rejected.
    Replay old;
    assert(!deserializeReplay("FASTTRIS_REPLAY 1\nseed 1\n",old,&err));
}

static void test_replay_validation_and_bounds() {
    Replay r;
    r.seed=9;
    r.mode=Mode::Zen;
    r.duration_us=10000;
    r.events={{9000,Action::Left,true},{8000,Action::Left,false}};
    Game g(r.seed,r.mode,r.rules);g.advanceTo(r.duration_us);r.final_hash=stateHash(g);
    std::string err;
    assert(!validateReplay(r,&err));
    assert(serializeReplay(r).empty());

    r.events={{1000,Action::Left,true}};
    r.duration_us=kMaxReplayDurationUs+1;
    assert(!validateReplay(r,&err));

    r.duration_us=10000;
    Game valid_game(r.seed,r.mode,r.rules);
    valid_game.advanceTo(1000);valid_game.press(Action::Left);valid_game.advanceTo(r.duration_us);
    r.final_hash=stateHash(valid_game);
    auto encoded=serializeReplay(r);
    assert(!encoded.empty());
    encoded.push_back('x');
    Replay trailing;
    assert(!deserializeReplay(encoded,trailing,&err));
}


static void test_replay_finalize_trims_post_finish_tail() {
    Rules rules;
    rules.custom_gravity_ms=0;
    Game game(20260831,Mode::Custom,rules);
    game.advanceTo(2500000);

    Replay replay;
    replay.seed=20260831;
    replay.mode=Mode::Custom;
    replay.rules=rules;
    replay.events={
        {1000000,Action::Left,true},
        {1500000,Action::Left,false},
        // Models the stale key-up/key-down tail that used to be recorded after
        // a finite goal froze Game::now().
        {3000000,Action::Right,true},
        {3100000,Action::Right,false}
    };

    finalizeReplay(replay,game);
    assert(replay.duration_us==2500000);
    assert(replay.events.size()==2);
    assert(replay.final_hash.has_value());
    assert(validateReplay(replay));
    assert(!serializeReplay(replay).empty());
}

static void test_replay_index_and_checkpoints() {
    Replay r;
    r.seed=1234567;
    r.mode=Mode::Zen;
    for(int sec=1;sec<=18;++sec){
        const TimeUs t=sec*1000000LL;
        r.events.push_back({t,Action::HardDrop,true});
    }
    r.duration_us=20000000;
    Game g(r.seed,r.mode,r.rules);
    for(const auto&e:r.events){g.advanceTo(e.time_us);g.press(e.action);}
    g.advanceTo(r.duration_us);
    r.final_hash=stateHash(g);

    ReplayIndexBuilder builder(r);
    int guard=0;
    while(!builder.finished()&&guard++<100000)assert(builder.step());
    assert(builder.finished());
    const auto& idx=builder.index();
    assert(idx.verification.has_value()&&*idx.verification);
    assert(idx.checkpoints.size()>=5);
    assert(idx.checkpoints.front().time_us==0);
    assert(idx.checkpoints.back().time_us==r.duration_us);
    assert(std::any_of(idx.checkpoints.begin(),idx.checkpoints.end(),[](const ReplayCheckpoint& cp){return cp.time_us==5000000;}));
    assert(std::any_of(idx.checkpoints.begin(),idx.checkpoints.end(),[](const ReplayCheckpoint& cp){return cp.time_us==10000000;}));
    assert(!idx.pieces.empty());

    // Restore a 10-second snapshot and reproduce only the short tail to 12 s.
    auto it=std::find_if(idx.checkpoints.begin(),idx.checkpoints.end(),[](const ReplayCheckpoint& cp){return cp.time_us==10000000;});
    assert(it!=idx.checkpoints.end());
    Game from_checkpoint=it->game;
    std::size_t event=it->event_index;
    constexpr TimeUs target=12000000;
    while(event<r.events.size()&&r.events[event].time_us<=target){
        from_checkpoint.advanceTo(r.events[event].time_us);from_checkpoint.press(r.events[event].action);++event;
    }
    from_checkpoint.advanceTo(target);

    Game from_start(r.seed,r.mode,r.rules);
    for(const auto&e:r.events){if(e.time_us>target)break;from_start.advanceTo(e.time_us);from_start.press(e.action);}from_start.advanceTo(target);
    assert(stateHash(from_checkpoint)==stateHash(from_start));
}

static void test_sha() {
    const std::string expected="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    assert(sha256("abc")==expected);
    Sha256 streaming;streaming.update("a");streaming.update("b");streaming.update("c");assert(streaming.finalHex()==expected);
}

static void test_irs_ihs() {
    Rules r;
    r.handling.irs=true;
    r.handling.ihs=true;
    r.handling.allow_180=true;
    Game g(4242,Mode::Zen,r);
    g.press(Action::RotateCW); // held
    g.press(Action::HardDrop);
    assert(!g.gameOver());
    assert(g.active().rot==Rotation::Right); // IRS applied to spawned piece.
    g.release(Action::RotateCW);

    Game h(4242,Mode::Zen,r);
    h.press(Action::Hold); // held through lock
    h.press(Action::HardDrop);
    assert(!h.gameOver());
    assert(h.holdUsed()); // IHS consumed hold on the new spawn.
    h.release(Action::Hold);
}

static void test_replay_fuzz() {
    Replay r;
    r.seed=0x123456789abcdef0ULL;
    r.mode=Mode::Zen;
    Pcg32 gen(99,123);
    TimeUs t=0;
    constexpr Action acts[]={Action::Left,Action::Right,Action::SoftDrop,Action::HardDrop,Action::RotateCW,Action::RotateCCW,Action::Rotate180,Action::Hold};
    for(int i=0;i<1500;++i){
        t += 200 + gen.bounded(6000);
        Action a=acts[gen.bounded(8)];
        bool down=(a==Action::HardDrop||a==Action::Hold||a==Action::RotateCW||a==Action::RotateCCW||a==Action::Rotate180) ? true : (gen.bounded(2)!=0);
        r.events.push_back({t,a,down});
        if(down && (a==Action::Hold||a==Action::RotateCW||a==Action::RotateCCW||a==Action::Rotate180))
            r.events.push_back({t+1,a,false});
    }
    r.duration_us=t+100000;
    Game g(r.seed,r.mode,r.rules);
    for(auto&e:r.events){g.advanceTo(e.time_us);if(e.down)g.press(e.action);else g.release(e.action);}
    g.advanceTo(r.duration_us);
    r.final_hash=stateHash(g);
    assert(verifyReplay(r));
}


static void test_default_horizontal_handling() {
    Rules r;
    assert(r.handling.das_ms==180);
    assert(r.handling.arr_ms==50);

    Game tap(2026,Mode::Zen,r);
    const int start_x=tap.active().x;
    tap.press(Action::Left);
    assert(tap.active().x==start_x-1); // key-down always moves exactly one cell.
    tap.advanceTo(160000);            // still inside the 180 ms DAS window.
    assert(tap.active().x==start_x-1);
    tap.release(Action::Left);
    tap.advanceTo(500000);
    assert(tap.active().x==start_x-1); // releasing before DAS must never auto-shift.

    Game held(2026,Mode::Zen,r);
    const int held_start=held.active().x;
    held.press(Action::Right);
    assert(held.active().x==held_start+1);
    held.advanceTo(179999);
    assert(held.active().x==held_start+1);
    held.advanceTo(180000);
    assert(held.active().x==held_start+2); // first repeat after DAS.
    held.advanceTo(229999);
    assert(held.active().x==held_start+2); // no second repeat before the full ARR interval.
    held.advanceTo(230000);
    assert(held.active().x==held_start+3); // then one cell per ARR interval.
}

static void test_zero_arr_remains_expert_instant_shift() {
    Rules r;
    r.handling.das_ms=100;
    r.handling.arr_ms=0;
    Game g(9001,Mode::Zen,r);
    g.press(Action::Left);
    const int after_press=g.active().x;
    g.advanceTo(99999);
    assert(g.active().x==after_press);
    g.advanceTo(100000);
    assert(g.active().x<=0); // zero ARR intentionally shifts to the wall after DAS.
}

static void test_zero_arr_preserves_charge_across_spawn() {
    Rules r;
    r.handling.das_ms=50;
    r.handling.arr_ms=0;
    Game g(31337,Mode::Zen,r);
    g.press(Action::Left);
    g.advanceTo(50000);
    assert(g.active().x<=0);
    g.press(Action::HardDrop);
    assert(!g.gameOver());
    assert(g.active().x<=0); // charged ARR 0 carries into the next piece.
    g.release(Action::Left);
}


static void test_modern_scoring() {
    auto pc_quad=scoreClear(ClearKind::Quad,0,false,true,1);
    assert(pc_quad.points==2800); // 800 Quad + 2000 Perfect Clear.
    assert(pc_quad.attack==14);

    auto b2b_pc_quad=scoreClear(ClearKind::Quad,0,true,true,1);
    assert(b2b_pc_quad.points==4400); // 1200 B2B Quad + 3200 B2B Quad PC bonus.

    auto combo=scoreClear(ClearKind::Single,4,false,false,2);
    assert(combo.points==600); // (100 + 4*50) * level 2.

}

static void test_finesse_tracking() {
    Rules r;
    Game clean(7007,Mode::Finesse,r);
    clean.press(Action::HardDrop);
    assert(clean.stats().pieces==1);
    assert(clean.stats().finesse_faults==0);
    assert(clean.stats().finesse_perfect_pieces==1);
    assert(clean.stats().finesse_streak==1);

    Game wasteful(7007,Mode::Finesse,r);
    wasteful.press(Action::Left); wasteful.release(Action::Left);
    wasteful.press(Action::Right); wasteful.release(Action::Right);
    wasteful.press(Action::HardDrop);
    assert(wasteful.stats().pieces==1);
    assert(wasteful.stats().finesse_faults>=2);
    assert(wasteful.stats().finesse_perfect_pieces==0);
    assert(wasteful.stats().finesse_streak==0);

    Game rotate_o(2,Mode::Finesse,r); // seed 2 starts with O.
    assert(rotate_o.active().piece==Piece::O);
    rotate_o.press(Action::RotateCW); rotate_o.release(Action::RotateCW);
    rotate_o.press(Action::HardDrop);
    assert(rotate_o.stats().finesse_faults>=1); // rotating O is unnecessary finesse.
}

static void test_custom_mode_rules() {
    Rules timed;
    timed.custom_time_limit_s=2;
    timed.custom_gravity_ms=0;
    Game t(991,Mode::Custom,timed);
    const int start_y=t.active().y;
    t.advanceTo(1999999);
    assert(!t.complete());
    assert(t.active().y==start_y);
    t.advanceTo(2000000);
    assert(t.complete());
    assert(t.stats().elapsed_us==2000000);

    Rules garbage;
    garbage.custom_gravity_ms=0;
    garbage.custom_start_garbage=6;
    Game a(12345,Mode::Custom,garbage), b(12345,Mode::Custom,garbage);
    for(int y=0;y<kBoardH;++y) assert(a.board().rowMask(y)==b.board().rowMask(y));
    int occupied_rows=0;
    for(int y=0;y<kBoardH;++y) if(a.board().rowMask(y)!=0) ++occupied_rows;
    assert(occupied_rows==6);
}


static void test_mode_basics() {
    Rules r;
    Game sprint(55,Mode::Sprint40,r);
    const int sy=sprint.active().y;
    sprint.advanceTo(999999);
    assert(sprint.active().y==sy);
    sprint.advanceTo(1000000);
    assert(sprint.active().y==sy+1);

    Game zen(55,Mode::Zen,r);
    const int zy=zen.active().y;
    zen.advanceTo(5000000);
    assert(zen.active().y==zy);

    Rules ultra_rules=r;
    ultra_rules.handling.lock_delay_ms=200000; // keep the first piece alive past the timer.
    Game ultra(55,Mode::Ultra120,ultra_rules);
    ultra.advanceTo(119999999);
    assert(!ultra.complete());
    ultra.advanceTo(120000000);
    assert(ultra.complete());
    assert(ultra.stats().elapsed_us==120000000);

    Game cheese(55,Mode::Cheese40,r);
    int cheese_rows=0;
    for(int y=0;y<kBoardH;++y) if(cheese.board().rowMask(y)!=0) ++cheese_rows;
    assert(cheese_rows==10);
}

static void test_replay_parser_requires_current_layout() {
    auto write_and_reject=[](const std::string& name,const std::string& body){
        auto path=(std::filesystem::temp_directory_path()/name).string();
        std::ofstream out(path,std::ios::binary);
        out<<body;
        out.close();
        Replay loaded;
        std::string err;
        assert(!loadReplay(path,loaded,&err));
        assert(!err.empty());
        std::filesystem::remove(path);
    };

    write_and_reject("fastris_replay_incomplete.ftr",
        "FASTTRIS_REPLAY 1\nseed 8080\nmode 0\nduration 0\nhash deadbeef\n");
    write_and_reject("fastris_replay_unknown_field.ftr",
        "FASTTRIS_REPLAY 1\nseed 8080\nmode 0\nobsolete 1\nduration 0\nhash deadbeef\n");
}


static void test_replay_incremental_decoder_and_action_filter() {
    Replay r;
    r.seed=77;
    r.mode=Mode::Zen;
    TimeUs t=0;
    for(int i=0;i<5000;++i){
        t+=1000;
        r.events.push_back({t,(i%2)?Action::Left:Action::Right,true});
        r.events.push_back({t+1,(i%2)?Action::Left:Action::Right,false});
    }
    r.duration_us=t+1000;
    Game g(r.seed,r.mode,r.rules);
    for(const auto& e:r.events){g.advanceTo(e.time_us);if(e.down)g.press(e.action);else g.release(e.action);}
    g.advanceTo(r.duration_us);
    r.final_hash=stateHash(g);
    const auto encoded=serializeReplay(r);
    ReplayDecoder decoder(encoded);
    std::size_t calls=0;
    while(!decoder.finished()){
        const auto before=decoder.decodedEvents();
        decoder.step(17);
        assert(decoder.decodedEvents()-before<=17);
        assert(++calls<10000);
    }
    assert(decoder.ok());
    auto decoded=decoder.takeReplay();
    assert(decoded.events==r.events);
    assert(decoded.final_hash==r.final_hash);

    r.events={{1000,Action::Pause,true}};
    r.duration_us=2000;
    r.final_hash=stateHash(Game(r.seed,r.mode,r.rules));
    std::string err;
    assert(!validateReplay(r,&err));
    r.events={{1000,Action::Restart,true}};
    assert(!validateReplay(r,&err));

    // The binary decoder independently rejects application-only actions too.
    Replay one;
    one.seed=88;one.mode=Mode::Zen;one.duration_us=2000;
    one.events={{1000,Action::Hold,true}};
    Game one_game(one.seed,one.mode,one.rules);
    one_game.advanceTo(1000);one_game.press(Action::Hold);one_game.advanceTo(one.duration_us);
    one.final_hash=stateHash(one_game);
    auto bad_action=serializeReplay(one);
    assert(bad_action.size()>33);
    bad_action[bad_action.size()-33]=static_cast<char>(static_cast<std::uint8_t>(Action::Pause)|0x80u);
    Replay rejected;
    assert(!deserializeReplay(bad_action,rejected,&err));
}

static void test_exact_semantic_events_and_replay_markers() {
    Rules r;
    Game g(123,Mode::Zen,r);
    g.setSemanticEventCapture(true);
    g.advanceTo(123456);
    g.press(Action::HardDrop);
    const auto events=g.semanticEvents();
    assert(!events.empty());
    assert(events.front().kind==GameEventKind::PieceLocked);
    assert(events.front().time_us==123456);
    assert(events.front().value==1);
    g.clearSemanticEvents();

    Replay replay;
    replay.seed=123;
    replay.mode=Mode::Zen;
    replay.events={{1000000,Action::HardDrop,true},{2000000,Action::HardDrop,true},{3000000,Action::HardDrop,true}};
    replay.duration_us=4000000;
    Game source(replay.seed,replay.mode,replay.rules);
    for(const auto& e:replay.events){source.advanceTo(e.time_us);source.press(e.action);}
    source.advanceTo(replay.duration_us);
    replay.final_hash=stateHash(source);
    ReplayIndexBuilder builder(replay);
    int guard=0;
    while(!builder.finished()&&guard++<100000)assert(builder.step());
    assert(builder.finished());
    const auto& pieces=builder.index().pieces;
    assert(pieces.size()>=3);
    assert(pieces[0].time_us==1000000);
    assert(pieces[1].time_us==2000000);
    assert(pieces[2].time_us==3000000);
}

static void test_adaptive_checkpoint_bound() {
    Replay max_replay;
    max_replay.seed=5;
    max_replay.mode=Mode::Zen;
    max_replay.duration_us=kMaxReplayDurationUs;
    Game max_game(max_replay.seed,max_replay.mode,max_replay.rules);
    max_game.advanceTo(max_replay.duration_us);
    max_replay.final_hash=stateHash(max_game);
    ReplayIndexBuilder max_builder(max_replay);
    const auto interval=max_builder.index().checkpoint_interval_us;
    assert(interval>=kReplayCheckpointBaseIntervalUs);
    const auto predicted=static_cast<std::size_t>((max_replay.duration_us-1)/interval)+2u;
    assert(predicted<=kMaxReplayCheckpoints);

    // Finish a multi-hour no-gravity replay to prove the runtime index itself
    // respects the cap and still preserves the final seek checkpoint.
    Replay long_replay;
    long_replay.seed=6;long_replay.mode=Mode::Zen;long_replay.duration_us=3LL*60LL*60LL*1000000LL;
    Game g(long_replay.seed,long_replay.mode,long_replay.rules);g.advanceTo(long_replay.duration_us);
    long_replay.final_hash=stateHash(g);
    ReplayIndexBuilder builder(long_replay);
    int guard=0;
    while(!builder.finished()&&guard++<100000)assert(builder.step());
    assert(builder.finished());
    assert(builder.index().checkpoints.size()<=kMaxReplayCheckpoints);
    assert(builder.index().checkpoints.back().time_us==long_replay.duration_us);
}

static void test_battle_smoke() {
    Rules r; r.garbage_delay_ms=0; Battle b(1,2,r,r); b.advanceTo(1000); b.press(0,Action::HardDrop); b.advanceTo(2000); assert(!b.game(0).gameOver()); assert(!b.game(1).gameOver());
}

int main(){
    test_seed_text_scanning();test_rng_and_bag();test_shapes();test_board();test_game_determinism();test_seed_difference();test_replay();test_replay_validation_and_bounds();test_replay_finalize_trims_post_finish_tail();test_replay_index_and_checkpoints();test_sha();test_irs_ihs();test_replay_fuzz();test_default_horizontal_handling();test_zero_arr_remains_expert_instant_shift();test_zero_arr_preserves_charge_across_spawn();test_modern_scoring();test_finesse_tracking();test_custom_mode_rules();test_mode_basics();test_replay_parser_requires_current_layout();test_replay_incremental_decoder_and_action_filter();test_exact_semantic_events_and_replay_markers();test_adaptive_checkpoint_bound();test_battle_smoke();
    std::cout<<"FasTris core tests: PASS\n";
}
