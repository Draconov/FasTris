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

bool replayActionAllowed(Action a) {
    return static_cast<std::uint8_t>(a) <= static_cast<std::uint8_t>(Action::Hold);
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

TimeUs checkpointIntervalFor(TimeUs duration_us) {
    if(duration_us<=0)return kReplayCheckpointBaseIntervalUs;
    const std::uint64_t segments=std::max<std::uint64_t>(1,kMaxReplayCheckpoints-1u);
    const auto duration=static_cast<std::uint64_t>(duration_us);
    const auto needed=(duration+segments-1u)/segments;
    TimeUs interval=std::max<TimeUs>(kReplayCheckpointBaseIntervalUs,static_cast<TimeUs>(needed));
    // Round long-replay intervals up to a whole second. This keeps checkpoint
    // times human-readable and guarantees the hard count bound after rounding.
    constexpr TimeUs second=1'000'000;
    if(interval>kReplayCheckpointBaseIntervalUs)interval=((interval+second-1)/second)*second;
    return interval;
}

} // namespace

Sha256Digest stateHash(const Game& g){
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
    return h.finalBytes();
}

bool validateReplay(const Replay& r,std::string* err){
    if(static_cast<std::uint8_t>(r.mode)>static_cast<std::uint8_t>(Mode::Custom)){setError(err,"invalid mode");return false;}
    if(!validRules(r.rules)){setError(err,"replay rules are out of range");return false;}
    if(r.duration_us<0||r.duration_us>kMaxReplayDurationUs){setError(err,"replay duration is out of range");return false;}
    if(r.events.size()>kMaxReplayEvents){setError(err,"replay contains too many events");return false;}
    TimeUs previous=0;
    bool first=true;
    for(const auto& e:r.events){
        if(!replayActionAllowed(e.action)){setError(err,"replay contains a non-gameplay action");return false;}
        if(e.time_us<0||e.time_us>r.duration_us){setError(err,"replay event is outside replay duration");return false;}
        if(!first&&e.time_us<previous){setError(err,"replay events are not monotonic");return false;}
        first=false;previous=e.time_us;
    }
    if(!r.final_hash.has_value()){setError(err,"replay is missing its final SHA-256 hash");return false;}
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
    w.raw(r.final_hash->data(),r.final_hash->size());
    return w.take();
}

ReplayDecoder::ReplayDecoder(std::span<const std::uint8_t> bytes):bytes_(bytes){
    if(bytes_.size()>kMaxReplayBytes)fail("replay file is too large");
}

void ReplayDecoder::fail(std::string_view message){
    if(error_.empty())error_=std::string(message);
    finished_=true;
}

bool ReplayDecoder::readByte(std::uint8_t& out){
    if(pos_>=bytes_.size())return false;
    out=bytes_[pos_++];
    return true;
}

bool ReplayDecoder::readRaw(void* out,std::size_t size){
    if(size>bytes_.size()-std::min(pos_,bytes_.size()))return false;
    std::memcpy(out,bytes_.data()+pos_,size);
    pos_+=size;
    return true;
}

bool ReplayDecoder::readVar(std::uint64_t& out){
    out=0;
    for(unsigned shift=0;shift<64;shift+=7){
        std::uint8_t b{};
        if(!readByte(b))return false;
        if(shift==63&&(b&0xfeu)!=0)return false;
        out|=std::uint64_t(b&0x7fu)<<shift;
        if((b&0x80u)==0)return true;
    }
    return false;
}

bool ReplayDecoder::readBoundedVar(std::uint64_t max,int& out){
    std::uint64_t v{};
    if(!readVar(v)||v>max)return false;
    out=static_cast<int>(v);
    return true;
}

bool ReplayDecoder::readRules(){
    auto& r=replay_.rules;
    auto& h=r.handling;
    if(!readBoundedVar(5000,h.das_ms))return false;
    if(!readBoundedVar(5000,h.arr_ms))return false;
    if(!readBoundedVar(1000,h.sdf))return false;
    if(!readBoundedVar(5000,h.dcd_ms))return false;
    if(!readBoundedVar(10000,h.lock_delay_ms))return false;
    if(!readBoundedVar(1000,h.max_lock_resets))return false;
    std::uint8_t flags{};
    if(!readByte(flags)||(flags&0xe0u)!=0)return false;
    h.allow_180=(flags&0x01u)!=0;
    h.irs=(flags&0x02u)!=0;
    h.ihs=(flags&0x04u)!=0;
    r.ghost=(flags&0x08u)!=0;
    r.tournament=(flags&0x10u)!=0;
    if(!readBoundedVar(8,r.next_count)||r.next_count<1)return false;
    if(!readBoundedVar(100,r.garbage_cap))return false;
    if(!readBoundedVar(60000,r.garbage_delay_ms))return false;
    if(!readBoundedVar(100,r.garbage_messiness_pct))return false;
    if(!readBoundedVar(60000,r.custom_gravity_ms))return false;
    if(!readBoundedVar(1000000,r.custom_line_goal))return false;
    if(!readBoundedVar(43200,r.custom_time_limit_s))return false;
    if(!readBoundedVar(12,r.custom_start_garbage))return false;
    return validRules(r);
}

bool ReplayDecoder::parseHeader(){
    std::array<std::uint8_t,kReplayMagic.size()> magic{};
    if(!readRaw(magic.data(),magic.size())||magic!=kReplayMagic){fail("unsupported replay format");return false;}
    std::uint64_t seed{};
    if(!readVar(seed)){fail("truncated replay seed");return false;}
    replay_.seed=seed;
    std::uint8_t mode{};
    if(!readByte(mode)||mode>static_cast<std::uint8_t>(Mode::Custom)){fail("invalid replay mode");return false;}
    replay_.mode=static_cast<Mode>(mode);
    if(!readRules()){fail("invalid replay rules");return false;}
    std::uint64_t duration{};
    if(!readVar(duration)||duration>static_cast<std::uint64_t>(kMaxReplayDurationUs)){fail("invalid replay duration");return false;}
    replay_.duration_us=static_cast<TimeUs>(duration);
    std::uint64_t count{};
    if(!readVar(count)||count>kMaxReplayEvents){fail("invalid replay event count");return false;}
    expected_events_=static_cast<std::size_t>(count);
    // Avoid one giant reserve allocation for hostile million-event files. The
    // vector grows normally while Web decoding yields between bounded chunks.
    replay_.events.reserve(std::min<std::size_t>(expected_events_,65'536u));
    header_done_=true;
    return true;
}

bool ReplayDecoder::finishDecode(){
    Sha256Digest hash{};
    if(!readRaw(hash.data(),hash.size())){fail("truncated replay hash");return false;}
    if(pos_!=bytes_.size()){fail("unexpected trailing replay data");return false;}
    replay_.final_hash=hash;
    finished_=true;
    return true;
}

bool ReplayDecoder::step(std::size_t max_events){
    if(finished_)return false;
    if(!header_done_&& !parseHeader())return false;
    if(max_events==0)max_events=1;
    std::size_t processed=0;
    while(decoded_events_<expected_events_&&processed<max_events){
        std::uint64_t delta{};std::uint8_t packed{};
        if(!readVar(delta)||!readByte(packed)){fail("truncated replay event data");return false;}
        if((packed&0x70u)!=0){fail("invalid replay event flags");return false;}
        const auto action_raw=static_cast<std::uint8_t>(packed&0x0fu);
        if(action_raw>static_cast<std::uint8_t>(Action::Hold)){fail("replay contains a non-gameplay action");return false;}
        if(delta>std::numeric_limits<std::uint64_t>::max()-timestamp_){fail("replay timestamp overflow");return false;}
        timestamp_+=delta;
        if(timestamp_>static_cast<std::uint64_t>(replay_.duration_us)||timestamp_>static_cast<std::uint64_t>(std::numeric_limits<TimeUs>::max())){
            fail("replay event is outside replay duration");return false;
        }
        replay_.events.push_back({static_cast<TimeUs>(timestamp_),static_cast<Action>(action_raw),(packed&0x80u)!=0});
        ++decoded_events_;++processed;
    }
    if(decoded_events_==expected_events_)return finishDecode();
    return processed>0;
}

Replay ReplayDecoder::takeReplay(){
    if(!ok())return {};
    return std::move(replay_);
}

bool deserializeReplay(std::string_view bytes,Replay& out,std::string* err){
    ReplayDecoder decoder(bytes);
    while(!decoder.finished())decoder.step(65'536u);
    if(!decoder.ok()){setError(err,decoder.error());out=Replay{};return false;}
    out=decoder.takeReplay();
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

bool verifyReplay(const Replay&r,Sha256Digest*actual){
    if(!validateReplay(r,nullptr))return false;
    Game g(r.seed,r.mode,r.rules);
    for(const auto&e:r.events){
        g.advanceTo(e.time_us);
        if(e.down)g.press(e.action);else g.release(e.action);
    }
    g.advanceTo(r.duration_us);
    const auto h=stateHash(g);
    if(actual)*actual=h;
    return h==*r.final_hash;
}

ReplayIndexBuilder::ReplayIndexBuilder(const Replay& replay)
    : replay_(&replay), game_(replay.seed,replay.mode,replay.rules) {
    index_.checkpoint_interval_us=checkpointIntervalFor(replay.duration_us);
    const auto interval=std::max<TimeUs>(1,index_.checkpoint_interval_us);
    const auto count=static_cast<std::size_t>(replay.duration_us/interval)+2u;
    index_.checkpoints.reserve(std::min<std::size_t>(count,kMaxReplayCheckpoints));
    index_.pieces.reserve(replay.events.size()/3u+1u);
    index_.line_clears.reserve(replay.events.size()/8u+1u);
    index_.tspins.reserve(replay.events.size()/32u+1u);
    index_.perfect_clears.reserve(replay.events.size()/128u+1u);
    game_.setSemanticEventCapture(true);
    next_checkpoint_us_=index_.checkpoint_interval_us;
    addCheckpoint(0);
}

void ReplayIndexBuilder::recordGameEvents(std::size_t next_event){
    for(const auto& e:game_.semanticEvents()){
        ReplayMarker marker{e.time_us,next_event,e.value};
        switch(e.kind){
            case GameEventKind::PieceLocked:index_.pieces.push_back(marker);break;
            case GameEventKind::LinesCleared:index_.line_clears.push_back(marker);break;
            case GameEventKind::TSpin:index_.tspins.push_back(marker);break;
            case GameEventKind::PerfectClear:index_.perfect_clears.push_back(marker);break;
        }
    }
    game_.clearSemanticEvents();
}

void ReplayIndexBuilder::advanceAndRecord(TimeUs target){
    if(target<=playhead_)return;
    game_.advanceTo(target);
    playhead_=target;
    recordGameEvents(event_index_);
}

void ReplayIndexBuilder::addCheckpoint(TimeUs at){
    Game snapshot=game_;
    snapshot.setSemanticEventCapture(false);
    if(!index_.checkpoints.empty()&&index_.checkpoints.back().time_us==at){
        index_.checkpoints.back()={at,event_index_,std::move(snapshot)};
        return;
    }
    if(index_.checkpoints.size()<kMaxReplayCheckpoints)index_.checkpoints.push_back({at,event_index_,std::move(snapshot)});
}

bool ReplayIndexBuilder::step(){
    if(finished_||!replay_)return false;
    const auto& r=*replay_;

    const TimeUs event_time=(event_index_<r.events.size())?r.events[event_index_].time_us:r.duration_us;
    const TimeUs checkpoint_time=(next_checkpoint_us_<r.duration_us)?next_checkpoint_us_:r.duration_us;
    const TimeUs slice_time=std::min(r.duration_us,playhead_+kReplayIndexSimulationSliceUs);
    const TimeUs target=std::min({event_time,checkpoint_time,slice_time,r.duration_us});

    if(target>playhead_){
        advanceAndRecord(target);
        if(target==next_checkpoint_us_&&next_checkpoint_us_<r.duration_us){
            addCheckpoint(target);
            next_checkpoint_us_+=index_.checkpoint_interval_us;
        }
        if(target<event_time)return true;
    }

    if(event_index_<r.events.size()&&r.events[event_index_].time_us==playhead_){
        const TimeUs at=playhead_;
        do{
            const auto& e=r.events[event_index_];
            if(e.down)game_.press(e.action);else game_.release(e.action);
            ++event_index_;
            recordGameEvents(event_index_);
        }while(event_index_<r.events.size()&&r.events[event_index_].time_us==at);
        return true;
    }

    if(playhead_<r.duration_us)return true;

    if(index_.checkpoints.empty()||index_.checkpoints.back().time_us!=r.duration_us)addCheckpoint(r.duration_us);
    if(r.final_hash.has_value())index_.verification=stateHash(game_)==*r.final_hash;
    finished_=true;
    return true;
}

} // namespace fasttris
