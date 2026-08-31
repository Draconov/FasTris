#include "fasttris/replay.hpp"
#include "fasttris/sha256.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <type_traits>

namespace fasttris {
namespace {
constexpr std::array<std::uint8_t,8> kReplayMagic{{'F','A','S','T','R','I','S',0x01}};

void setError(std::string* err, std::string_view message) {
    if (err) *err = std::string(message);
}

class ByteWriter {
public:
    void reserve(std::size_t size) { data_.reserve(size); }
    void byte(std::uint8_t v) { data_.push_back(static_cast<char>(v)); }
    void raw(const void* data, std::size_t size) {
        const auto* p = static_cast<const char*>(data);
        data_.append(p, size);
    }
    void var(std::uint64_t v) {
        while (v >= 0x80) {
            byte(static_cast<std::uint8_t>((v & 0x7fu) | 0x80u));
            v >>= 7;
        }
        byte(static_cast<std::uint8_t>(v));
    }
    std::string take() { return std::move(data_); }
private:
    std::string data_;
};

class ByteReader {
public:
    explicit ByteReader(std::string_view bytes)
        : cur_(reinterpret_cast<const std::uint8_t*>(bytes.data())),
          end_(cur_ + bytes.size()) {}

    bool raw(void* out, std::size_t size) {
        if (remaining() < size) return false;
        std::memcpy(out, cur_, size);
        cur_ += size;
        return true;
    }
    bool byte(std::uint8_t& out) {
        if (cur_ == end_) return false;
        out = *cur_++;
        return true;
    }
    bool var(std::uint64_t& out) {
        out = 0;
        for (unsigned shift = 0; shift < 64; shift += 7) {
            std::uint8_t b{};
            if (!byte(b)) return false;
            if (shift == 63 && (b & 0xfeu) != 0) return false;
            out |= std::uint64_t(b & 0x7fu) << shift;
            if ((b & 0x80u) == 0) return true;
        }
        return false;
    }
    std::size_t remaining() const { return static_cast<std::size_t>(end_ - cur_); }
    bool empty() const { return cur_ == end_; }
private:
    const std::uint8_t* cur_{};
    const std::uint8_t* end_{};
};

template <class T>
void hashUnsignedLE(Sha256& h, T value) {
    static_assert(std::is_unsigned_v<T>);
    std::array<std::uint8_t,sizeof(T)> bytes{};
    for (std::size_t i=0;i<bytes.size();++i) bytes[i]=static_cast<std::uint8_t>(value>>(8*i));
    h.update(bytes.data(),bytes.size());
}

void hashI32(Sha256& h, int value) { hashUnsignedLE(h, static_cast<std::uint32_t>(value)); }
void hashI64(Sha256& h, TimeUs value) { hashUnsignedLE(h, static_cast<std::uint64_t>(value)); }
void hashBool(Sha256& h, bool value) { const std::uint8_t v=value?1u:0u; h.update(&v,1); }
void hashByte(Sha256& h, std::uint8_t value) { h.update(&value,1); }

void hashRules(Sha256& h,const Rules& r){
    const auto& x=r.handling;
    hashI32(h,x.das_ms);hashI32(h,x.arr_ms);hashI32(h,x.sdf);hashI32(h,x.dcd_ms);
    hashI32(h,x.lock_delay_ms);hashI32(h,x.max_lock_resets);
    hashBool(h,x.allow_180);hashBool(h,x.irs);hashBool(h,x.ihs);
    hashBool(h,r.ghost);hashI32(h,r.next_count);hashBool(h,r.tournament);
    hashI32(h,r.garbage_cap);hashI32(h,r.garbage_delay_ms);hashI32(h,r.garbage_messiness_pct);
    hashI32(h,r.custom_gravity_ms);hashI32(h,r.custom_line_goal);hashI32(h,r.custom_time_limit_s);hashI32(h,r.custom_start_garbage);
}

bool validRules(const Rules& r) {
    const auto& h=r.handling;
    if(h.das_ms<0||h.das_ms>5000) return false;
    if(h.arr_ms<0||h.arr_ms>5000) return false;
    if(h.sdf<0||h.sdf>1000) return false;
    if(h.dcd_ms<0||h.dcd_ms>5000) return false;
    if(h.lock_delay_ms<0||h.lock_delay_ms>10000) return false;
    if(h.max_lock_resets<0||h.max_lock_resets>1000) return false;
    if(r.next_count<1||r.next_count>8) return false;
    if(r.garbage_cap<0||r.garbage_cap>100) return false;
    if(r.garbage_delay_ms<0||r.garbage_delay_ms>60000) return false;
    if(r.garbage_messiness_pct<0||r.garbage_messiness_pct>100) return false;
    if(r.custom_gravity_ms<0||r.custom_gravity_ms>60000) return false;
    if(r.custom_line_goal<0||r.custom_line_goal>1000000) return false;
    if(r.custom_time_limit_s<0||r.custom_time_limit_s>43200) return false;
    if(r.custom_start_garbage<0||r.custom_start_garbage>12) return false;
    return true;
}

void writeRules(ByteWriter& w,const Rules& r){
    const auto& h=r.handling;
    w.var(static_cast<std::uint64_t>(h.das_ms));
    w.var(static_cast<std::uint64_t>(h.arr_ms));
    w.var(static_cast<std::uint64_t>(h.sdf));
    w.var(static_cast<std::uint64_t>(h.dcd_ms));
    w.var(static_cast<std::uint64_t>(h.lock_delay_ms));
    w.var(static_cast<std::uint64_t>(h.max_lock_resets));
    std::uint8_t flags=0;
    flags|=h.allow_180?0x01u:0u;
    flags|=h.irs?0x02u:0u;
    flags|=h.ihs?0x04u:0u;
    flags|=r.ghost?0x08u:0u;
    flags|=r.tournament?0x10u:0u;
    w.byte(flags);
    w.var(static_cast<std::uint64_t>(r.next_count));
    w.var(static_cast<std::uint64_t>(r.garbage_cap));
    w.var(static_cast<std::uint64_t>(r.garbage_delay_ms));
    w.var(static_cast<std::uint64_t>(r.garbage_messiness_pct));
    w.var(static_cast<std::uint64_t>(r.custom_gravity_ms));
    w.var(static_cast<std::uint64_t>(r.custom_line_goal));
    w.var(static_cast<std::uint64_t>(r.custom_time_limit_s));
    w.var(static_cast<std::uint64_t>(r.custom_start_garbage));
}

bool readBoundedVar(ByteReader& rd,std::uint64_t max,int& out){
    std::uint64_t v{};
    if(!rd.var(v)||v>max)return false;
    out=static_cast<int>(v);
    return true;
}

bool readRules(ByteReader& rd,Rules& r){
    auto& h=r.handling;
    if(!readBoundedVar(rd,5000,h.das_ms))return false;
    if(!readBoundedVar(rd,5000,h.arr_ms))return false;
    if(!readBoundedVar(rd,1000,h.sdf))return false;
    if(!readBoundedVar(rd,5000,h.dcd_ms))return false;
    if(!readBoundedVar(rd,10000,h.lock_delay_ms))return false;
    if(!readBoundedVar(rd,1000,h.max_lock_resets))return false;
    std::uint8_t flags{};
    if(!rd.byte(flags)||(flags&0xe0u)!=0)return false;
    h.allow_180=(flags&0x01u)!=0;
    h.irs=(flags&0x02u)!=0;
    h.ihs=(flags&0x04u)!=0;
    r.ghost=(flags&0x08u)!=0;
    r.tournament=(flags&0x10u)!=0;
    if(!readBoundedVar(rd,8,r.next_count)||r.next_count<1)return false;
    if(!readBoundedVar(rd,100,r.garbage_cap))return false;
    if(!readBoundedVar(rd,60000,r.garbage_delay_ms))return false;
    if(!readBoundedVar(rd,100,r.garbage_messiness_pct))return false;
    if(!readBoundedVar(rd,60000,r.custom_gravity_ms))return false;
    if(!readBoundedVar(rd,1000000,r.custom_line_goal))return false;
    if(!readBoundedVar(rd,43200,r.custom_time_limit_s))return false;
    if(!readBoundedVar(rd,12,r.custom_start_garbage))return false;
    return true;
}

void addRepeatedMarkers(std::vector<ReplayMarker>& out,int before,int after,TimeUs at,std::size_t next_event){
    for(int value=before+1;value<=after;++value)out.push_back({at,next_event,value});
}

} // namespace


std::string stateHash(const Game& g){
    Sha256 h;
    static constexpr std::string_view tag="FASTRIS_STATE_BINARY_1";
    h.update(tag);
    hashUnsignedLE(h,g.seed_);
    hashByte(h,static_cast<std::uint8_t>(g.mode_));
    hashRules(h,g.rules_);

    for(int y=0;y<kBoardH;++y)for(int x=0;x<kBoardW;++x)hashByte(h,static_cast<std::uint8_t>(g.board_.cell(x,y)));

    hashByte(h,static_cast<std::uint8_t>(g.active_.piece));
    hashByte(h,static_cast<std::uint8_t>(g.active_.rot));
    hashI32(h,g.active_.x);hashI32(h,g.active_.y);
    hashByte(h,static_cast<std::uint8_t>(g.hold_));hashBool(h,g.hold_used_);

    const auto& s=g.stats_;
    hashUnsignedLE(h,static_cast<std::uint64_t>(s.score));
    hashI32(h,s.lines);hashI32(h,s.pieces);hashI32(h,s.attacks);hashI32(h,s.inputs);hashI32(h,s.holds);
    hashI32(h,s.rotations);hashI32(h,s.hard_drops);hashI32(h,s.soft_drop_cells);hashI32(h,s.quads);hashI32(h,s.tspins);
    hashI32(h,s.perfect_clears);hashI32(h,s.combo);hashI32(h,s.max_combo);hashI32(h,s.b2b_chain);hashI32(h,s.max_b2b);
    hashI32(h,s.finesse_faults);hashI32(h,s.finesse_perfect_pieces);hashI32(h,s.finesse_streak);hashI32(h,s.max_finesse_streak);
    hashI32(h,s.garbage_lines_cleared);hashI64(h,s.elapsed_us);

    const auto& q=g.bag_.queue();
    hashUnsignedLE(h,static_cast<std::uint32_t>(q.size()));
    for(auto p:q)hashByte(h,static_cast<std::uint8_t>(p));
    hashUnsignedLE(h,g.bag_.rngState());hashUnsignedLE(h,g.bag_.rngStream());

    const auto& packets=g.garbage_.packets();
    hashUnsignedLE(h,static_cast<std::uint32_t>(packets.size()));
    for(const auto& p:packets){hashI32(h,p.lines);hashI64(h,p.ready_us);hashI32(h,p.hole);}
    hashUnsignedLE(h,g.garbage_.rngState());hashUnsignedLE(h,g.garbage_.rngStream());hashI32(h,g.garbage_.lastHole());
    hashUnsignedLE(h,g.cheese_rng_.state());hashUnsignedLE(h,g.cheese_rng_.stream());

    hashI64(h,g.now_us_);hashI64(h,g.next_gravity_us_);hashI64(h,g.lock_deadline_us_);hashI64(h,g.next_horizontal_us_);
    hashBool(h,g.grounded_);hashI32(h,g.lock_resets_);hashBool(h,g.game_over_);hashBool(h,g.complete_);hashBool(h,g.paused_);
    hashBool(h,g.left_held_);hashBool(h,g.right_held_);hashBool(h,g.soft_held_);hashBool(h,g.cw_held_);hashBool(h,g.ccw_held_);
    hashBool(h,g.rot180_held_);hashBool(h,g.hold_held_);hashI32(h,g.horiz_dir_);hashI32(h,g.outgoing_attack_);hashI32(h,g.last_attack_visual_);
    hashBool(h,g.last_action_rotation_);hashI32(h,g.last_kick_index_);hashByte(h,static_cast<std::uint8_t>(g.last_clear_));
    hashI32(h,g.piece_input_count_);hashI32(h,g.piece_spawn_x_);hashByte(h,static_cast<std::uint8_t>(g.piece_spawn_rot_));
    return h.finalHex();
}

bool validateReplay(const Replay& r,std::string* err){
    if(static_cast<std::uint8_t>(r.mode)>static_cast<std::uint8_t>(Mode::Custom)){setError(err,"invalid mode");return false;}
    if(!validRules(r.rules)){setError(err,"replay rules are out of range");return false;}
    if(r.duration_us<0||r.duration_us>kMaxReplayDurationUs){setError(err,"replay duration is out of range");return false;}
    if(r.events.size()>kMaxReplayEvents){setError(err,"replay contains too many events");return false;}
    TimeUs previous=0;
    bool first=true;
    for(const auto& e:r.events){
        if(static_cast<std::uint8_t>(e.action)>=static_cast<std::uint8_t>(Action::Count)){setError(err,"replay contains an invalid action");return false;}
        if(e.time_us<0||e.time_us>r.duration_us){setError(err,"replay event is outside replay duration");return false;}
        if(!first&&e.time_us<previous){setError(err,"replay events are not monotonic");return false;}
        first=false;previous=e.time_us;
    }
    std::array<std::uint8_t,32> hash{};
    if(!parseHex32(r.final_hash,hash)){setError(err,"replay hash is not a valid SHA-256 value");return false;}
    return true;
}

std::string serializeReplay(const Replay&r){
    if(!validateReplay(r,nullptr))return {};
    ByteWriter w;
    w.reserve(96u+r.events.size()*5u);
    w.raw(kReplayMagic.data(),kReplayMagic.size());
    w.var(r.seed);
    w.byte(static_cast<std::uint8_t>(r.mode));
    writeRules(w,r.rules);
    w.var(static_cast<std::uint64_t>(r.duration_us));
    w.var(static_cast<std::uint64_t>(r.events.size()));
    TimeUs previous=0;
    for(const auto& e:r.events){
        const auto delta=static_cast<std::uint64_t>(e.time_us-previous);
        w.var(delta);
        const auto action=static_cast<std::uint8_t>(e.action);
        w.byte(static_cast<std::uint8_t>(action|(e.down?0x80u:0u)));
        previous=e.time_us;
    }
    std::array<std::uint8_t,32> hash{};
    if(!parseHex32(r.final_hash,hash))return {};
    w.raw(hash.data(),hash.size());
    return w.take();
}

bool deserializeReplay(std::string_view bytes,Replay& out,std::string* err){
    out=Replay{};
    if(bytes.size()>kMaxReplayBytes){setError(err,"replay file is too large");return false;}
    ByteReader rd(bytes);
    std::array<std::uint8_t,kReplayMagic.size()> magic{};
    if(!rd.raw(magic.data(),magic.size())||magic!=kReplayMagic){setError(err,"unsupported replay format");return false;}

    std::uint64_t seed{};
    if(!rd.var(seed)){setError(err,"truncated replay seed");return false;}
    out.seed=seed;
    std::uint8_t mode{};
    if(!rd.byte(mode)||mode>static_cast<std::uint8_t>(Mode::Custom)){setError(err,"invalid replay mode");return false;}
    out.mode=static_cast<Mode>(mode);
    if(!readRules(rd,out.rules)){setError(err,"invalid replay rules");return false;}

    std::uint64_t duration{};
    if(!rd.var(duration)||duration>static_cast<std::uint64_t>(kMaxReplayDurationUs)){setError(err,"invalid replay duration");return false;}
    out.duration_us=static_cast<TimeUs>(duration);
    std::uint64_t event_count{};
    if(!rd.var(event_count)||event_count>kMaxReplayEvents){setError(err,"invalid replay event count");return false;}
    out.events.clear();out.events.reserve(static_cast<std::size_t>(event_count));

    std::uint64_t timestamp=0;
    for(std::uint64_t i=0;i<event_count;++i){
        std::uint64_t delta{};std::uint8_t packed{};
        if(!rd.var(delta)||!rd.byte(packed)){setError(err,"truncated replay event data");return false;}
        if((packed&0x70u)!=0){setError(err,"invalid replay event flags");return false;}
        const auto action=static_cast<std::uint8_t>(packed&0x0fu);
        if(action>=static_cast<std::uint8_t>(Action::Count)){setError(err,"invalid replay action");return false;}
        if(delta>std::numeric_limits<std::uint64_t>::max()-timestamp){setError(err,"replay timestamp overflow");return false;}
        timestamp+=delta;
        if(timestamp>duration||timestamp>static_cast<std::uint64_t>(std::numeric_limits<TimeUs>::max())){setError(err,"replay event is outside replay duration");return false;}
        out.events.push_back({static_cast<TimeUs>(timestamp),static_cast<Action>(action),(packed&0x80u)!=0});
    }

    std::array<std::uint8_t,32> hash{};
    if(!rd.raw(hash.data(),hash.size())){setError(err,"truncated replay hash");return false;}
    if(!rd.empty()){setError(err,"unexpected trailing replay data");return false;}
    out.final_hash=hexLower(hash.data(),hash.size());
    if(!validateReplay(out,err))return false;
    return true;
}

bool saveReplay(const Replay&r,const std::string&path,std::string*err){
    if(!validateReplay(r,err))return false;
    const auto bytes=serializeReplay(r);
    if(bytes.empty()){setError(err,"could not encode replay");return false;}
    std::ofstream f(path,std::ios::binary);
    if(!f){setError(err,"cannot open replay for writing");return false;}
    f.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    if(!f){setError(err,"failed writing replay");return false;}
    return true;
}

bool loadReplay(const std::string&path,Replay&out,std::string*err){
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if(!f){setError(err,"cannot open replay");return false;}
    const auto end=f.tellg();
    if(end<0){setError(err,"failed reading replay size");return false;}
    const auto size=static_cast<std::uint64_t>(end);
    if(size>kMaxReplayBytes){setError(err,"replay file is too large");return false;}
    std::string bytes(static_cast<std::size_t>(size),'\0');
    f.seekg(0,std::ios::beg);
    if(size>0)f.read(bytes.data(),static_cast<std::streamsize>(size));
    if(!f&&size>0){setError(err,"failed reading replay");return false;}
    return deserializeReplay(bytes,out,err);
}

bool verifyReplay(const Replay&r,std::string*actual){
    if(!validateReplay(r,nullptr))return false;
    Game g(r.seed,r.mode,r.rules);
    for(const auto&e:r.events){
        g.advanceTo(e.time_us);
        if(e.down)g.press(e.action);else g.release(e.action);
    }
    g.advanceTo(r.duration_us);
    const auto h=stateHash(g);
    if(actual)*actual=h;
    return h==r.final_hash;
}

ReplayIndexBuilder::ReplayIndexBuilder(const Replay& replay)
    : replay_(&replay), game_(replay.seed,replay.mode,replay.rules) {
    const auto checkpoint_count=static_cast<std::size_t>(replay.duration_us/std::max<TimeUs>(1,kReplayCheckpointIntervalUs))+2u;
    index_.checkpoints.reserve(checkpoint_count);
    index_.pieces.reserve(replay.events.size()/3u+1u);
    index_.line_clears.reserve(replay.events.size()/8u+1u);
    index_.tspins.reserve(replay.events.size()/32u+1u);
    index_.perfect_clears.reserve(replay.events.size()/128u+1u);
    index_.checkpoints.push_back({0,0,game_});
}

void ReplayIndexBuilder::recordStats(const Stats& before,const Stats& after,TimeUs at,std::size_t next_event){
    addRepeatedMarkers(index_.pieces,before.pieces,after.pieces,at,next_event);
    if(after.lines>before.lines)index_.line_clears.push_back({at,next_event,after.lines});
    addRepeatedMarkers(index_.tspins,before.tspins,after.tspins,at,next_event);
    addRepeatedMarkers(index_.perfect_clears,before.perfect_clears,after.perfect_clears,at,next_event);
}

void ReplayIndexBuilder::advanceAndRecord(TimeUs target){
    if(target<=playhead_)return;
    const auto before=game_.stats();
    game_.advanceTo(target);
    playhead_=target;
    recordStats(before,game_.stats(),playhead_,event_index_);
}

void ReplayIndexBuilder::addCheckpoint(TimeUs at){
    if(!index_.checkpoints.empty()&&index_.checkpoints.back().time_us==at){
        index_.checkpoints.back()={at,event_index_,game_};
        return;
    }
    index_.checkpoints.push_back({at,event_index_,game_});
}

bool ReplayIndexBuilder::step(){
    if(finished_||!replay_)return false;
    const auto& r=*replay_;

    // A single builder step never asks Game::advanceTo() to consume an
    // unbounded span. This keeps pathological high-gravity/custom replays from
    // monopolizing a Web frame while still letting the outer loop spend its
    // normal ~1.5 ms budget efficiently.
    const TimeUs event_time=(event_index_<r.events.size())?r.events[event_index_].time_us:r.duration_us;
    const TimeUs checkpoint_time=(next_checkpoint_us_<r.duration_us)?next_checkpoint_us_:r.duration_us;
    const TimeUs slice_time=std::min(r.duration_us,playhead_+kReplayIndexSimulationSliceUs);
    const TimeUs target=std::min({event_time,checkpoint_time,slice_time,r.duration_us});

    if(target>playhead_){
        advanceAndRecord(target);
        if(target==next_checkpoint_us_&&next_checkpoint_us_<r.duration_us){
            addCheckpoint(target);
            next_checkpoint_us_+=kReplayCheckpointIntervalUs;
        }
        if(target<event_time)return true;
    }

    if(event_index_<r.events.size()&&r.events[event_index_].time_us==playhead_){
        const TimeUs at=playhead_;
        do{
            const auto before=game_.stats();
            const auto& e=r.events[event_index_];
            if(e.down)game_.press(e.action);else game_.release(e.action);
            ++event_index_;
            recordStats(before,game_.stats(),at,event_index_);
        }while(event_index_<r.events.size()&&r.events[event_index_].time_us==at);
        if(next_checkpoint_us_==at&&next_checkpoint_us_<r.duration_us){
            addCheckpoint(at);
            next_checkpoint_us_+=kReplayCheckpointIntervalUs;
        }
        return true;
    }

    if(playhead_<r.duration_us)return true;

    if(index_.checkpoints.empty()||index_.checkpoints.back().time_us!=r.duration_us)addCheckpoint(r.duration_us);
    if(!r.final_hash.empty())index_.verification=stateHash(game_)==r.final_hash;
    finished_=true;
    return true;
}

} // namespace fasttris
