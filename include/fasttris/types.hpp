#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace fasttris {

using TimeUs = std::int64_t;
constexpr int kBoardW = 10;
constexpr int kVisibleH = 20;
constexpr int kHiddenH = 4;
constexpr int kBoardH = kVisibleH + kHiddenH;
constexpr TimeUs kNever = (std::int64_t{1} << 61);

enum class Piece : std::uint8_t { None=0, I, J, L, O, S, T, Z, Garbage };
enum class Rotation : std::uint8_t { Spawn=0, Right=1, Reverse=2, Left=3 };
enum class Action : std::uint8_t {
    Left, Right, SoftDrop, HardDrop, RotateCW, RotateCCW, Rotate180, Hold,
    Restart, Pause, Count
};
enum class Mode : std::uint8_t { Sprint40, Ultra120, Marathon, Zen, Cheese40, Finesse, SeedRace, Custom };
enum class Spin : std::uint8_t { None, Mini, Full };

enum class GameEventKind : std::uint8_t { PieceLocked, LinesCleared, TSpin, PerfectClear };
struct GameEvent {
    TimeUs time_us{};
    GameEventKind kind{GameEventKind::PieceLocked};
    int value{};
};

enum class ClearKind : std::uint8_t {
    None, Single, Double, Triple, Quad,
    MiniNoLine, MiniSingle, MiniDouble, TSpinNoLine, TSpinSingle, TSpinDouble, TSpinTriple
};

struct Vec2 { int x{}; int y{}; };
struct ActivePiece {
    Piece piece{Piece::None};
    Rotation rot{Rotation::Spawn};
    int x{3};
    int y{3};
};

struct Handling {
    int das_ms{180};
    int arr_ms{50};
    int sdf{20};                  // 0 = sonic soft drop
    int dcd_ms{0};
    int lock_delay_ms{500};
    int max_lock_resets{15};
    bool allow_180{true};
    bool irs{true};
    bool ihs{true};
};


struct Rules {
    Handling handling{};
    bool ghost{true};
    int next_count{5};
    bool tournament{false};
    int garbage_cap{8};
    int garbage_delay_ms{500};
    int garbage_messiness_pct{25};

    // Custom / Sandbox mode only. Zero means disabled/endless.
    int custom_gravity_ms{1000};
    int custom_line_goal{0};
    int custom_time_limit_s{0};
    int custom_start_garbage{0};
};

struct Stats {
    std::int64_t score{};
    int lines{};
    int pieces{};
    int attacks{};
    int inputs{};
    int holds{};
    int rotations{};
    int hard_drops{};
    int soft_drop_cells{};
    int quads{};
    int tspins{};
    int perfect_clears{};
    int combo{-1};
    int max_combo{-1};
    int b2b_chain{};
    int max_b2b{};
    int finesse_faults{};
    int finesse_perfect_pieces{};
    int finesse_streak{};
    int max_finesse_streak{};
    int garbage_lines_cleared{};
    TimeUs elapsed_us{};
};

inline constexpr std::string_view pieceName(Piece p) {
    switch (p) {
        case Piece::I: return "I"; case Piece::J: return "J"; case Piece::L: return "L";
        case Piece::O: return "O"; case Piece::S: return "S"; case Piece::T: return "T";
        case Piece::Z: return "Z"; case Piece::Garbage: return "G"; default: return "-";
    }
}
inline constexpr std::string_view actionName(Action a) {
    switch (a) {
        case Action::Left: return "left"; case Action::Right: return "right";
        case Action::SoftDrop: return "soft"; case Action::HardDrop: return "hard";
        case Action::RotateCW: return "cw"; case Action::RotateCCW: return "ccw";
        case Action::Rotate180: return "180"; case Action::Hold: return "hold";
        case Action::Restart: return "restart"; case Action::Pause: return "pause";
        default: return "unknown";
    }
}
inline constexpr std::string_view modeName(Mode m) {
    switch (m) {
        case Mode::Sprint40: return "Sprint 40L"; case Mode::Ultra120: return "Ultra 2:00";
        case Mode::Marathon: return "Marathon"; case Mode::Zen: return "Zen";
        case Mode::Cheese40: return "Cheese 40"; case Mode::Finesse: return "Finesse";
        case Mode::SeedRace: return "Seed Race"; case Mode::Custom: return "Custom";
    }
    return "Unknown";
}

} // namespace fasttris
