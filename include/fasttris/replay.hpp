#pragma once
#include "game.hpp"
#include <string>
#include <vector>
namespace fasttris {
struct ReplayEvent { TimeUs time_us{}; Action action{Action::Left}; bool down{}; };
struct Replay {
    int version{1};std::uint64_t seed{1};Mode mode{Mode::Sprint40};Rules rules{};TimeUs duration_us{};
    std::vector<ReplayEvent> events;std::string final_hash;
};
std::string stateHash(const Game& game);
bool saveReplay(const Replay& r,const std::string& path,std::string* error=nullptr);
bool loadReplay(const std::string& path,Replay& out,std::string* error=nullptr);
bool verifyReplay(const Replay& r,std::string* actual_hash=nullptr);
const char* actionToken(Action a);
bool parseActionToken(const std::string& s,Action& a);
} // namespace fasttris
