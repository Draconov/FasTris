#include "fasttris/bag.hpp"
#include "fasttris/battle.hpp"
#include "fasttris/board.hpp"
#include "fasttris/game.hpp"
#include "fasttris/replay.hpp"
#include "fasttris/scoring.hpp"
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
    auto path=(std::filesystem::temp_directory_path()/"fasttris_test.ftr").string();std::string err;assert(saveReplay(r,path,&err));Replay x;assert(loadReplay(path,x,&err));assert(verifyReplay(x));std::filesystem::remove(path);
}
static void test_sha() { assert(sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"); }

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
    assert(r.handling.das_ms==140);
    assert(r.handling.arr_ms==25);

    Game tap(2026,Mode::Zen,r);
    const int start_x=tap.active().x;
    tap.press(Action::Left);
    assert(tap.active().x==start_x-1); // key-down always moves exactly one cell.
    tap.advanceTo(120000);            // still inside the 140 ms DAS window.
    assert(tap.active().x==start_x-1);
    tap.release(Action::Left);
    tap.advanceTo(500000);
    assert(tap.active().x==start_x-1); // releasing before DAS must never auto-shift.

    Game held(2026,Mode::Zen,r);
    const int held_start=held.active().x;
    held.press(Action::Right);
    assert(held.active().x==held_start+1);
    held.advanceTo(139999);
    assert(held.active().x==held_start+1);
    held.advanceTo(140000);
    assert(held.active().x==held_start+2); // first repeat after DAS.
    held.advanceTo(165000);
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
    auto pc_quad=scoreClear(ClearKind::Quad,0,false,true,1,kSimulationRulesVersion);
    assert(pc_quad.points==2800); // 800 Quad + 2000 Perfect Clear.
    assert(pc_quad.attack==14);

    auto b2b_pc_quad=scoreClear(ClearKind::Quad,0,true,true,1,kSimulationRulesVersion);
    assert(b2b_pc_quad.points==4400); // 1200 B2B Quad + 3200 B2B Quad PC bonus.

    auto combo=scoreClear(ClearKind::Single,4,false,false,2,kSimulationRulesVersion);
    assert(combo.points==600); // (100 + 4*50) * level 2.

    auto legacy=scoreClear(ClearKind::Single,0,false,true,1,1);
    assert(legacy.points==3600); // Preserve original replay scoring when simver=1.
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

static void test_legacy_replay_without_simver() {
    Rules rules;
    rules.simulation_version=1;
    rules.handling.das_ms=100;
    rules.handling.arr_ms=0;
    Replay r;
    r.seed=8080;
    r.mode=Mode::Sprint40;
    r.rules=rules;
    r.events={{1000,Action::RotateCW,true},{1001,Action::RotateCW,false},{2000,Action::HardDrop,true}};
    r.duration_us=5000;
    Game g(r.seed,r.mode,r.rules);
    for(auto&e:r.events){g.advanceTo(e.time_us);if(e.down)g.press(e.action);else g.release(e.action);}
    g.advanceTo(r.duration_us);
    r.final_hash=stateHash(g);

    auto path=(std::filesystem::temp_directory_path()/"fastris_legacy_replay.ftr").string();
    std::ofstream out(path,std::ios::binary);
    out<<"FASTTRIS_REPLAY 1\nseed "<<r.seed<<"\nmode "<<static_cast<int>(r.mode)<<"\n"
       <<"das 100\narr 0\nsdf 20\ndcd 0\nlock 500\nresets 15\nrot180 1\nirs 1\nihs 1\nghost 1\nnext 5\ntournament 0\ngcap 8\ngdelay 500\ngmess 25\n";
    for(auto&e:r.events)out<<"event "<<e.time_us<<' '<<actionToken(e.action)<<' '<<(e.down?1:0)<<"\n";
    out<<"duration "<<r.duration_us<<"\nhash "<<r.final_hash<<"\n";
    out.close();

    Replay loaded;
    std::string err;
    assert(loadReplay(path,loaded,&err));
    assert(loaded.rules.simulation_version==1);
    assert(verifyReplay(loaded));
    std::filesystem::remove(path);
}

static void test_battle_smoke() {
    Rules r; r.garbage_delay_ms=0; Battle b(1,2,r,r); b.advanceTo(1000); b.press(0,Action::HardDrop); b.advanceTo(2000); assert(!b.game(0).gameOver()); assert(!b.game(1).gameOver());
}

int main(){
    test_rng_and_bag();test_shapes();test_board();test_game_determinism();test_seed_difference();test_replay();test_sha();test_irs_ihs();test_replay_fuzz();test_default_horizontal_handling();test_zero_arr_remains_expert_instant_shift();test_zero_arr_preserves_charge_across_spawn();test_modern_scoring();test_finesse_tracking();test_custom_mode_rules();test_mode_basics();test_legacy_replay_without_simver();test_battle_smoke();
    std::cout<<"FasTris core tests: PASS\n";
}
