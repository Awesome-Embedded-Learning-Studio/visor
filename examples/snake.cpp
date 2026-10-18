// A monochrome snake: fixed steps, buffered turns, and cell-sized repaints.
#include "demo_font.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"
#include "visor/bindings/offscreen/offscreen_host.hpp"
#include "visor/bindings/x11/x11_window_host.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/control_style.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/widget.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <thread>
#include <utility>

namespace {

namespace draw = visor::draw;

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::canvas::Canvas;
using visor::canvas::ColorRole;
using visor::canvas::Ink;
using visor::examples::kDemoFont;
using visor::host::KeyDown;
using visor::host::OffscreenHost;
using visor::ui::CanvasPainter;
using visor::ui::CornerRadius;
using visor::ui::DirtyRegions;
using visor::ui::InputRouter;
using visor::ui::Painter;
using visor::ui::Theme;
using visor::ui::Widget;

constexpr SizeI kScreen{.w = 480, .h = 320};
using Frame = Canvas<kScreen>;
constexpr RectI kScreenBounds{.pos = {0, 0}, .size = kScreen};

constexpr int kColumns = 30;
constexpr int kRows = 20;
constexpr int kCellSize = 12;
constexpr int kCapacity = kColumns * kRows;
constexpr RectI kBoard{
    .pos = {60, 64},
    .size = {.w = kColumns * kCellSize, .h = kRows * kCellSize},
};
constexpr RectI kBoardFrame{
    .pos = {58, 62},
    .size = {.w = kBoard.size.w + 4, .h = kBoard.size.h + 4},
};
constexpr RectI kHeaderRule{.pos = {18, 54}, .size = {.w = 444, .h = 1}};
constexpr RectI kFooterRule{.pos = {18, 308}, .size = {.w = 444, .h = 1}};
constexpr RectI kStatusBounds{.pos = {330, 10}, .size = {.w = 132, .h = 40}};
constexpr RectI kOverlay{.pos = {140, 157}, .size = {.w = 200, .h = 54}};

constexpr Theme kNight{
    .background = Ink::black(),
    .foreground = Ink::white(),
    .muted = Ink::white(),
    .accent = Ink::white(),
    .on_accent = Ink::black(),
    .border = Ink::white(),
    .disabled = Ink::white(),
    .corner_radius = {.top_left = 3, .top_right = 3, .bottom_right = 3, .bottom_left = 3},
};
constexpr Theme kPaper{
    .background = Ink::white(),
    .foreground = Ink::black(),
    .muted = Ink::black(),
    .accent = Ink::black(),
    .on_accent = Ink::white(),
    .border = Ink::black(),
    .disabled = Ink::black(),
    .corner_radius = {.top_left = 3, .top_right = 3, .bottom_right = 3, .bottom_left = 3},
};

struct Cell {
    int x = 0;
    int y = 0;

    constexpr bool operator==(const Cell&) const = default;
};

enum class Direction : std::uint8_t { Up, Down, Left, Right };

constexpr Cell step_of(Direction direction) noexcept {
    switch (direction) {
        case Direction::Up:
            return {.x = 0, .y = -1};
        case Direction::Down:
            return {.x = 0, .y = 1};
        case Direction::Left:
            return {.x = -1, .y = 0};
        case Direction::Right:
            return {.x = 1, .y = 0};
    }
    return {.x = 0, .y = 0};
}

constexpr bool is_opposite(Direction first, Direction second) noexcept {
    return (first == Direction::Up && second == Direction::Down) ||
           (first == Direction::Down && second == Direction::Up) ||
           (first == Direction::Left && second == Direction::Right) ||
           (first == Direction::Right && second == Direction::Left);
}

/**
 * @brief The rules, without a painter: a fixed grid, a body array, a food and
 *        a two-slot turn queue.
 *
 * The simulation is deliberately a plain object. tick() advances it, on_draw()
 * only reads it, and the two-slot queue is what makes a fast "up, then left"
 * between two steps come out as two right-angle turns instead of a reversal.
 * A step that is not running returns false and changes nothing, so the panel's
 * frame loop can keep calling it while the game is paused or over.
 */
class SnakeGame {
  public:
    enum class State : std::uint8_t { Ready, Running, Paused, Over, Won };

    explicit SnakeGame(std::uint32_t seed = 0x5EEDU) noexcept { reset(seed); }

    void reset(std::uint32_t seed) noexcept;
    void start() noexcept;
    void pause() noexcept;
    void resume() noexcept;
    void turn(Direction direction) noexcept;
    /// One simulation step. True when the board changed (a move, a meal, a
    /// death, a win); false when the game was not running.
    bool step() noexcept;

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] int score() const noexcept { return eaten_ * 10; }
    [[nodiscard]] int length() const noexcept { return length_; }
    [[nodiscard]] int speed_level() const noexcept { return 1 + (eaten_ / 5); }
    [[nodiscard]] std::uint64_t step_interval_ms() const noexcept;
    [[nodiscard]] Cell head() const noexcept { return body_[0]; }
    [[nodiscard]] Cell food() const noexcept { return food_; }
    [[nodiscard]] Direction direction() const noexcept { return direction_; }
    [[nodiscard]] const std::array<Cell, kCapacity>& body() const noexcept { return body_; }
    [[nodiscard]] static bool inside(Cell cell) noexcept;
    /// Whether the next head may land on cell: the tail is excluded unless
    /// the move eats, because a non-growing step vacates the tail.
    [[nodiscard]] bool safe_for_next(Cell cell) const noexcept;
    /// Scripted demos and the self-test place food directly; the normal game
    /// only gets it from place_food.
    void set_food_for_test(Cell cell) noexcept;

  private:
    [[nodiscard]] bool occupied(Cell cell) const noexcept;
    void place_food() noexcept;
    std::uint32_t next_random() noexcept;

    std::array<Cell, kCapacity> body_{};
    Cell food_{};
    Direction direction_ = Direction::Right;
    std::array<Direction, 2> queued_{};
    int queued_count_ = 0;
    int length_ = 3;
    int eaten_ = 0;
    State state_ = State::Ready;
    std::uint32_t rng_ = 0x5EEDU;
};

void SnakeGame::reset(std::uint32_t seed) noexcept {
    rng_ = seed == 0 ? 0x5EEDU : seed;
    length_ = 3;
    body_[0] = Cell{.x = kColumns / 2, .y = kRows / 2};
    body_[1] = Cell{.x = body_[0].x - 1, .y = body_[0].y};
    body_[2] = Cell{.x = body_[0].x - 2, .y = body_[0].y};
    direction_ = Direction::Right;
    queued_count_ = 0;
    eaten_ = 0;
    state_ = State::Ready;
    place_food();
}

void SnakeGame::start() noexcept {
    if (state_ == State::Ready) {
        state_ = State::Running;
    }
}

void SnakeGame::pause() noexcept {
    if (state_ == State::Running) {
        state_ = State::Paused;
    }
}

void SnakeGame::resume() noexcept {
    if (state_ == State::Paused) {
        state_ = State::Running;
    }
}

void SnakeGame::turn(Direction direction) noexcept {
    if (state_ != State::Running && state_ != State::Paused) {
        return;
    }
    if (queued_count_ == 2) {
        return;
    }
    const Direction reference =
        queued_count_ > 0 ? queued_[static_cast<std::size_t>(queued_count_ - 1)] : direction_;
    if (direction == reference || is_opposite(direction, reference)) {
        return;
    }
    queued_[static_cast<std::size_t>(queued_count_)] = direction;
    ++queued_count_;
}

bool SnakeGame::step() noexcept {
    if (state_ != State::Running) {
        return false;
    }
    if (queued_count_ > 0) {
        direction_ = queued_[0];
        for (int i = 1; i < queued_count_; ++i) {
            queued_[static_cast<std::size_t>(i - 1)] = queued_[static_cast<std::size_t>(i)];
        }
        --queued_count_;
    }
    const Cell head = body_[0];
    const Cell next{
        .x = head.x + step_of(direction_).x,
        .y = head.y + step_of(direction_).y,
    };
    const bool eating = next == food_;
    if (!safe_for_next(next)) {
        state_ = State::Over;
        return true;
    }
    if (eating && length_ >= kCapacity) {
        state_ = State::Won;
        return true;
    }
    if (eating) {
        for (int i = length_; i > 0; --i) {
            body_[static_cast<std::size_t>(i)] = body_[static_cast<std::size_t>(i - 1)];
        }
        ++length_;
        ++eaten_;
        place_food();
    } else {
        for (int i = length_ - 1; i > 0; --i) {
            body_[static_cast<std::size_t>(i)] = body_[static_cast<std::size_t>(i - 1)];
        }
    }
    body_[0] = next;
    return true;
}

std::uint64_t SnakeGame::step_interval_ms() const noexcept {
    const int level = std::min(speed_level(), 7);
    return static_cast<std::uint64_t>(160 - ((level - 1) * 15));
}

bool SnakeGame::inside(Cell cell) noexcept {
    return cell.x >= 0 && cell.x < kColumns && cell.y >= 0 && cell.y < kRows;
}

bool SnakeGame::occupied(Cell cell) const noexcept {
    for (int i = 0; i < length_; ++i) {
        if (body_[static_cast<std::size_t>(i)] == cell) {
            return true;
        }
    }
    return false;
}

bool SnakeGame::safe_for_next(Cell cell) const noexcept {
    if (!inside(cell)) {
        return false;
    }
    const bool eating = cell == food_;
    const int count = eating ? length_ : length_ - 1;
    for (int i = 0; i < count; ++i) {
        if (body_[static_cast<std::size_t>(i)] == cell) {
            return false;
        }
    }
    return true;
}

void SnakeGame::set_food_for_test(Cell cell) noexcept {
    if (inside(cell) && !occupied(cell)) {
        food_ = cell;
    }
}

std::uint32_t SnakeGame::next_random() noexcept {
    rng_ ^= rng_ << 13U;
    rng_ ^= rng_ >> 17U;
    rng_ ^= rng_ << 5U;
    return rng_;
}

void SnakeGame::place_food() noexcept {
    const int start = static_cast<int>(next_random() % static_cast<std::uint32_t>(kCapacity));
    for (int i = 0; i < kCapacity; ++i) {
        const int index = (start + i) % kCapacity;
        const Cell candidate{.x = index % kColumns, .y = index / kColumns};
        if (!occupied(candidate)) {
            food_ = candidate;
            return;
        }
    }
    state_ = State::Won;
}

/**
 * @brief The panel: a fixed-step simulation under a 60 Hz frame loop.
 *
 * The root is the only focusable widget, so keys reach it without a child
 * control; the focus ring lands on the screen edge. tick() owns the
 * accumulator and the demo, on_draw() owns every pixel, and the board is the
 * only region a step reports -- the header and footer stay put.
 */
class SnakePanel final : public Widget {
  public:
    explicit SnakePanel(std::uint32_t seed = 0x5EEDU) noexcept;

    [[nodiscard]] bool initialize() noexcept;
    void tick(std::uint64_t dt_ms) noexcept;
    void advance_for(std::uint64_t ms) noexcept;
    void set_demo(bool on) noexcept;
    [[nodiscard]] bool self_test() noexcept;

    [[nodiscard]] bool focusable() const noexcept override { return true; }
    void on_key_down(std::uint32_t code, visor::host::Modifiers modifiers) noexcept override;

  protected:
    void on_draw(const Painter& painter) const noexcept override;

  private:
    void start_game() noexcept;
    void restart_game() noexcept;
    void advance_demo(std::uint64_t dt_ms) noexcept;
    [[nodiscard]] Direction choose_demo_direction() const noexcept;
    void draw_board(const Painter& painter, ColorRole fg, ColorRole bg) const noexcept;
    void draw_overlay(const Painter& painter, ColorRole fg, ColorRole bg) const noexcept;

    SnakeGame game_;
    std::uint32_t seed_ = 0x5EEDU;
    std::uint64_t accumulator_ = 0;
    std::uint64_t demo_delay_ = 0;
    int last_score_ = 0;
    int last_length_ = 3;
    int last_speed_ = 1;
    bool demo_ = false;
};

SnakePanel::SnakePanel(std::uint32_t seed) noexcept
    : Widget{kScreenBounds}, game_{seed}, seed_{seed} {
    set_theme(&kNight);
}

bool SnakePanel::initialize() noexcept {
    // The game is a single focusable root: no child control, no Tab walk. The
    // framework draws the holder's ring at the screen edge.
    set_focused(true);
    return true;
}

void SnakePanel::tick(std::uint64_t dt_ms) noexcept {
    if (demo_) {
        advance_demo(dt_ms);
    }
    if (game_.state() != SnakeGame::State::Running) {
        return;
    }
    accumulator_ += dt_ms;
    const std::uint64_t interval = game_.step_interval_ms();
    int steps = 0;
    while (accumulator_ >= interval && steps < 4) {
        accumulator_ -= interval;
        if (demo_) {
            game_.turn(choose_demo_direction());
        }
        static_cast<void>(game_.step());
        ++steps;
        const bool status_changed = game_.score() != last_score_ ||
                                    game_.length() != last_length_ ||
                                    game_.speed_level() != last_speed_;
        last_score_ = game_.score();
        last_length_ = game_.length();
        last_speed_ = game_.speed_level();
        invalidate(kBoardFrame);
        if (status_changed) {
            invalidate(kStatusBounds);
        }
        if (game_.state() != SnakeGame::State::Running) {
            invalidate(kBoardFrame);
            break;
        }
    }
    if (steps == 4) {
        accumulator_ = 0; // a long stall owes one step, not a teleport
    }
}

void SnakePanel::advance_for(std::uint64_t ms) noexcept {
    constexpr std::uint64_t step_size = 16;
    while (ms > 0) {
        const std::uint64_t step = ms > step_size ? step_size : ms;
        tick(step);
        ms -= step;
    }
}

void SnakePanel::set_demo(bool on) noexcept {
    if (demo_ == on) {
        return;
    }
    demo_ = on;
    demo_delay_ = 0;
}

void SnakePanel::start_game() noexcept {
    game_.start();
    accumulator_ = 0;
    demo_delay_ = 0;
    last_score_ = game_.score();
    last_length_ = game_.length();
    last_speed_ = game_.speed_level();
    invalidate(kBoardFrame);
    invalidate(kStatusBounds);
}

void SnakePanel::restart_game() noexcept {
    // A repeat game gets a different board; the seed advance keeps the run
    // reproducible from --seed while the demo never sits on the same food.
    seed_ = (seed_ * 1664525U) + 1013904223U;
    game_.reset(seed_);
    game_.start();
    accumulator_ = 0;
    demo_delay_ = 0;
    last_score_ = game_.score();
    last_length_ = game_.length();
    last_speed_ = game_.speed_level();
    invalidate(kBoardFrame);
    invalidate(kStatusBounds);
}

void SnakePanel::advance_demo(std::uint64_t dt_ms) noexcept {
    if (game_.state() == SnakeGame::State::Ready) {
        start_game();
        return;
    }
    if (game_.state() == SnakeGame::State::Over || game_.state() == SnakeGame::State::Won) {
        demo_delay_ += dt_ms;
        if (demo_delay_ >= 1200) {
            restart_game();
        }
    }
}

Direction SnakePanel::choose_demo_direction() const noexcept {
    constexpr std::array<Direction, 4> candidates{
        Direction::Up,
        Direction::Right,
        Direction::Down,
        Direction::Left,
    };
    const Cell head = game_.head();
    const Cell food = game_.food();
    int best_distance = 1'000'000;
    Direction best = game_.direction();
    for (const Direction candidate : candidates) {
        if (is_opposite(candidate, game_.direction())) {
            continue;
        }
        const Cell next{
            .x = head.x + step_of(candidate).x,
            .y = head.y + step_of(candidate).y,
        };
        if (!game_.safe_for_next(next)) {
            continue;
        }
        const int dx = next.x > food.x ? next.x - food.x : food.x - next.x;
        const int dy = next.y > food.y ? next.y - food.y : food.y - next.y;
        const int distance = dx + dy;
        if (distance < best_distance) {
            best_distance = distance;
            best = candidate;
        }
    }
    return best;
}

void SnakePanel::on_key_down(std::uint32_t code,
                             [[maybe_unused]] visor::host::Modifiers modifiers) noexcept {
    if (code == static_cast<std::uint32_t>('i') || code == static_cast<std::uint32_t>('I')) {
        set_theme(theme() == &kNight ? &kPaper : &kNight);
        return;
    }
    if (code == visor::host::kKeySpace || code == visor::host::kCharSpace) {
        switch (game_.state()) {
            case SnakeGame::State::Ready:
                start_game();
                break;
            case SnakeGame::State::Running:
                game_.pause();
                invalidate(kBoardFrame);
                break;
            case SnakeGame::State::Paused:
                game_.resume();
                invalidate(kBoardFrame);
                break;
            case SnakeGame::State::Over:
            case SnakeGame::State::Won:
                restart_game();
                break;
        }
        return;
    }
    if (code == visor::host::kKeyEnter) {
        switch (game_.state()) {
            case SnakeGame::State::Ready:
                start_game();
                break;
            case SnakeGame::State::Paused:
                game_.resume();
                invalidate(kBoardFrame);
                break;
            case SnakeGame::State::Over:
            case SnakeGame::State::Won:
                restart_game();
                break;
            case SnakeGame::State::Running:
                break;
        }
        return;
    }
    Direction direction = game_.direction();
    if (code == visor::host::kKeyUp) {
        direction = Direction::Up;
    } else if (code == visor::host::kKeyDown) {
        direction = Direction::Down;
    } else if (code == visor::host::kKeyLeft) {
        direction = Direction::Left;
    } else if (code == visor::host::kKeyRight) {
        direction = Direction::Right;
    } else {
        return;
    }
    if (game_.state() == SnakeGame::State::Running || game_.state() == SnakeGame::State::Paused) {
        game_.turn(direction);
    }
}

void SnakePanel::draw_board(const Painter& painter, ColorRole fg, ColorRole bg) const noexcept {
    paint_outline(painter, kBoardFrame, fg, bg,
                  CornerRadius{.top_left = 2, .top_right = 2, .bottom_right = 2, .bottom_left = 2});
    // A coarse lattice every two cells: enough to read the board's scale in
    // the empty stretches, sparse enough to stay behind the snake.
    constexpr int grid_step = kCellSize * 2;
    for (int x = kBoard.pos.x; x <= kBoard.pos.x + kBoard.size.w; x += grid_step) {
        for (int y = kBoard.pos.y; y <= kBoard.pos.y + kBoard.size.h; y += grid_step) {
            painter.plot(PointI{x, y}, fg);
        }
    }
    const Cell food = game_.food();
    const int food_x = kBoard.pos.x + (food.x * kCellSize) + (kCellSize / 2);
    const int food_y = kBoard.pos.y + (food.y * kCellSize) + (kCellSize / 2);
    painter.fill(RectI{.pos = {food_x - 3, food_y - 1}, .size = {.w = 6, .h = 2}}, fg);
    painter.fill(RectI{.pos = {food_x - 1, food_y - 3}, .size = {.w = 2, .h = 6}}, fg);

    const auto& body = game_.body();
    for (int i = game_.length() - 1; i >= 0; --i) {
        const Cell cell = body[static_cast<std::size_t>(i)];
        const RectI square{
            .pos =
                {
                    kBoard.pos.x + (cell.x * kCellSize) + 1,
                    kBoard.pos.y + (cell.y * kCellSize) + 1,
                },
            .size = {.w = kCellSize - 2, .h = kCellSize - 2},
        };
        painter.fill(square, fg);
        if (i != 0) {
            continue;
        }
        // The head's notch points where it is going; the body cells are plain.
        const RectI eye = [&] {
            switch (game_.direction()) {
                case Direction::Up:
                    return RectI{
                        .pos = {square.pos.x + 4, square.pos.y + 2},
                        .size = {.w = 2, .h = 2},
                    };
                case Direction::Down:
                    return RectI{
                        .pos = {square.pos.x + 4, square.pos.y + 8},
                        .size = {.w = 2, .h = 2},
                    };
                case Direction::Left:
                    return RectI{
                        .pos = {square.pos.x + 2, square.pos.y + 4},
                        .size = {.w = 2, .h = 2},
                    };
                case Direction::Right:
                    return RectI{
                        .pos = {square.pos.x + 8, square.pos.y + 4},
                        .size = {.w = 2, .h = 2},
                    };
            }
            return RectI{};
        }();
        painter.fill(eye, bg);
    }
}

void SnakePanel::draw_overlay(const Painter& painter, ColorRole fg, ColorRole bg) const noexcept {
    if (game_.state() == SnakeGame::State::Running) {
        return;
    }
    paint_outline(painter, kOverlay, fg, bg,
                  CornerRadius{.top_left = 3, .top_right = 3, .bottom_right = 3, .bottom_left = 3});
    const auto centered = [&](int y, const char* words, int scale) {
        const SizeI size = draw::measure_text(kDemoFont, words, scale);
        draw_text(painter, {kOverlay.pos.x + ((kOverlay.size.w - size.w) / 2), y}, kDemoFont, words,
                  fg, scale);
    };
    switch (game_.state()) {
        case SnakeGame::State::Ready:
            centered(168, "PRESS SPACE", 2);
            centered(190, "TO START", 1);
            break;
        case SnakeGame::State::Paused:
            centered(168, "PAUSED", 2);
            centered(190, "SPACE TO RESUME", 1);
            break;
        case SnakeGame::State::Over:
            centered(168, "GAME OVER", 2);
            centered(190, "ENTER TO RESTART", 1);
            break;
        case SnakeGame::State::Won:
            centered(168, "YOU WIN", 2);
            centered(190, "ENTER TO RESTART", 1);
            break;
        case SnakeGame::State::Running:
            break;
    }
}

void SnakePanel::on_draw(const Painter& painter) const noexcept {
    const auto fg = ColorRole::Foreground;
    const auto bg = ColorRole::Background;
    painter.fill(bounds(), bg);

    draw_text(painter, {18, 10}, kDemoFont, "VISOR // SNAKE", fg, 2);
    draw_text(painter, {200, 32}, kDemoFont, "MONO SNAKE / 01", fg, 1);
    std::array<char, 24> text{};
    const auto value_line = [&](int y, const char* format, int value) {
        const int count = std::snprintf(text.data(), text.size(), format, value);
        if (count > 0) {
            draw_text(painter, {330, y}, kDemoFont, text.data(), fg, 1);
        }
    };
    value_line(12, "SCORE %03d", game_.score());
    value_line(26, "LEN   %03d", game_.length());
    value_line(40, "SPEED %d", game_.speed_level());

    painter.fill(kHeaderRule, fg);
    draw_board(painter, fg, bg);
    draw_overlay(painter, fg, bg);
    painter.fill(kFooterRule, fg);
    draw_text(painter, {18, 312}, kDemoFont, "ARROWS TURN / SPACE PAUSE / ENTER RESTART / I INVERT",
              fg, 1);
    draw_text(painter, {396, 312}, kDemoFont, "VISOR / 0.1", fg, 1);
}

bool SnakePanel::self_test() noexcept {
    const auto require = [](bool ok, const char* what) {
        if (!ok) {
            std::println(stderr, "snake self-test: {}", what);
        }
        return ok;
    };

    // The rules, without a painter: movement, growth, the two-slot turn
    // buffer, the pause, the wall and the reset.
    {
        SnakeGame game(0x5EEDU);
        if (!require(game.state() == SnakeGame::State::Ready, "a fresh game is ready")) {
            return false;
        }
        if (!require(game.length() == 3, "a fresh snake has three cells")) {
            return false;
        }
        if (!require(game.score() == 0, "a fresh game scores zero")) {
            return false;
        }
        const Cell head = game.head();
        game.start();
        if (!require(game.step(), "a running step moved")) {
            return false;
        }
        if (!require(game.head().x == head.x + 1 && game.head().y == head.y,
                     "the snake starts east")) {
            return false;
        }
        if (!require(game.length() == 3, "a plain step keeps the length")) {
            return false;
        }
        game.set_food_for_test(Cell{.x = game.head().x + 1, .y = game.head().y});
        if (!require(game.step(), "the eating step moved")) {
            return false;
        }
        if (!require(game.length() == 4 && game.score() == 10, "eating grows and scores")) {
            return false;
        }
    }
    {
        SnakeGame game(0x5EEDU);
        game.start();
        const Cell head = game.head();
        game.turn(Direction::Up);
        game.turn(Direction::Left);
        game.step();
        if (!require(game.head().x == head.x && game.head().y == head.y - 1,
                     "the first queued turn goes up")) {
            return false;
        }
        game.step();
        if (!require(game.head().x == head.x - 1 && game.head().y == head.y - 1,
                     "the second queued turn goes left")) {
            return false;
        }
    }
    {
        SnakeGame game(0x5EEDU);
        game.start();
        const Cell head = game.head();
        game.turn(Direction::Left); // opposite of the current direction
        game.step();
        if (!require(game.head().x == head.x + 1, "a reversal is ignored")) {
            return false;
        }
        game.turn(Direction::Up);
        game.turn(Direction::Down); // opposite of the queued Up
        game.step();
        if (!require(game.head().y == head.y - 1 && game.head().x == head.x + 1,
                     "the queued up wins over the reversal")) {
            return false;
        }
    }
    {
        SnakeGame game(0x5EEDU);
        game.start();
        game.pause();
        const Cell head = game.head();
        if (!require(!game.step() && game.head() == head, "a paused game does not step")) {
            return false;
        }
        game.resume();
        if (!require(game.step(), "a resumed game moves")) {
            return false;
        }
    }
    {
        SnakeGame game(0x5EEDU);
        game.start();
        for (int i = 0; i < 40 && game.state() == SnakeGame::State::Running; ++i) {
            static_cast<void>(game.step());
        }
        if (!require(game.state() == SnakeGame::State::Over, "the east wall ends the run")) {
            return false;
        }
        game.reset(0x5EEDU);
        if (!require(game.state() == SnakeGame::State::Ready && game.length() == 3 &&
                         game.score() == 0,
                     "reset restores the opening board")) {
            return false;
        }
    }

    // The frame path: full frame versus 16-row strips, the ready tick's empty
    // ledger, a key-started step that owes the board, and a meal that owes the
    // score header.
    tick(0);
    Frame full;
    full.set_clear(false);
    draw(CanvasPainter{full, *theme()});
    OffscreenHost replay(kScreen.w, kScreen.h);
    Canvas<SizeI{.w = 480, .h = 16}> strip;
    visor::ui::render_strips(*this, strip, kScreen, replay.table());
    for (int y = 0; y < kScreen.h; ++y) {
        for (int x = 0; x < kScreen.w; ++x) {
            if (replay.get(PointI{x, y}) != full.get(PointI{x, y})) {
                return require(false, "strip replay differs from the whole frame");
            }
        }
    }

    DirtyRegions debts;
    set_dirty_sink(&debts);
    debts.clear();
    tick(10);
    if (!require(debts.count() == 0, "a ready tick owes pixels")) {
        return false;
    }

    OffscreenHost screen(kScreen.w, kScreen.h);
    InputRouter input;
    Canvas<SizeI{.w = 480, .h = 16}> event_strip;
    DirtyRegions event_debts;
    set_dirty_sink(&event_debts);
    event_debts.push(kScreenBounds);
    static_cast<void>(
        visor::ui::run_dirty(screen.table(), *this, input, event_strip, kScreen, event_debts));
    Frame before;
    before.set_clear(false);
    draw(CanvasPainter{before, *theme()});
    if (!require(screen.push_event(KeyDown{.code = visor::host::kKeySpace}),
                 "the event feed refused the space")) {
        return false;
    }
    static_cast<void>(
        visor::ui::run_dirty(screen.table(), *this, input, event_strip, kScreen, event_debts));
    if (!require(game_.state() == SnakeGame::State::Running, "space did not start the game")) {
        return false;
    }
    event_debts.clear();
    tick(10);
    if (!require(event_debts.count() == 0, "a tick shorter than the interval owes pixels")) {
        return false;
    }
    tick(200);
    if (!require(event_debts.touches(kBoardFrame), "a step does not owe the board")) {
        return false;
    }
    if (!require(!event_debts.touches(kHeaderRule), "a step repaints the header rule")) {
        return false;
    }
    int changed = 0;
    for (int y = kBoardFrame.pos.y; y < kBoardFrame.pos.y + kBoardFrame.size.h; ++y) {
        for (int x = kBoardFrame.pos.x; x < kBoardFrame.pos.x + kBoardFrame.size.w; ++x) {
            if (screen.get(PointI{x, y}) != before.get(PointI{x, y})) {
                ++changed;
            }
        }
    }
    if (!require(changed > 0, "the started step left the board unchanged")) {
        return false;
    }
    game_.set_food_for_test(Cell{.x = game_.head().x + 1, .y = game_.head().y});
    event_debts.clear();
    tick(200);
    if (!require(event_debts.touches(kBoardFrame) && event_debts.touches(kStatusBounds),
                 "a meal does not owe the board and the score header")) {
        return false;
    }
    if (!require(game_.score() == 10 && game_.length() == 4, "the meal did not score or grow")) {
        return false;
    }
    std::println("snake self-test: ok ({} board pixels changed by the start and step)", changed);
    return true;
}

bool save_snapshot(const char* path, const Frame& frame) noexcept {
    auto* file = std::fopen(path, "wb");
    if (file == nullptr) {
        return false;
    }
    std::println(file, "P4\n{} {}", kScreen.w, kScreen.h);
    for (const auto byte : frame.pixels()) {
        const auto inverted = static_cast<unsigned char>(255 - byte);
        if (std::fwrite(&inverted, 1, 1, file) != 1) {
            static_cast<void>(std::fclose(file));
            return false;
        }
    }
    return std::fclose(file) == 0;
}

} // namespace

int main(int argc, char* const* argv) {
    const char* snapshot_path = nullptr;
    std::uint64_t at_ms = 0;
    std::uint64_t frame_limit = 0;
    std::uint32_t seed = 0x5EEDU;
    bool self_test = false;
    bool demo = false;
    bool paper = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc) {
            snapshot_path = argv[++i];
        } else if (std::strcmp(argv[i], "--at-ms") == 0 && i + 1 < argc) {
            at_ms = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frame_limit = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--self-test") == 0) {
            self_test = true;
        } else if (std::strcmp(argv[i], "--demo") == 0) {
            demo = true;
        } else if (std::strcmp(argv[i], "--paper") == 0) {
            paper = true;
        } else {
            std::println(stderr, "visor: unknown argument: {}", argv[i]);
            return 1;
        }
    }

    SnakePanel panel{seed};
    if (!panel.initialize()) {
        std::println(stderr, "visor: the snake panel could not initialize");
        return 1;
    }
    if (paper) {
        panel.set_theme(&kPaper);
    }
    if (demo) {
        panel.set_demo(true);
    }
    if (self_test) {
        return panel.self_test() ? 0 : 1;
    }
    if (snapshot_path != nullptr) {
        panel.advance_for(at_ms);
        Frame frame;
        panel.draw(CanvasPainter{frame, *panel.theme()});
        if (!save_snapshot(snapshot_path, frame)) {
            std::println(stderr, "visor: cannot write the snapshot");
            return 1;
        }
        return 0;
    }

    auto opened = visor::host::x11::X11WindowHost::open(nullptr, kScreen.w, kScreen.h, 2);
    if (!opened.has_value()) {
        std::println(stderr, "visor: cannot open the display (is DISPLAY set?)");
        return 1;
    }
    auto host = std::move(*opened);
    InputRouter input;
    DirtyRegions dirty;
    panel.set_dirty_sink(&dirty);
    Canvas<SizeI{.w = 480, .h = 16}> strip;
    dirty.push(kScreenBounds);
    visor::ui::render_strips(panel, strip, kScreen, host.table(), dirty);
    dirty.clear();

    std::println(
        "visor: snake; arrows turn, Space pauses, Enter restarts, I inverts; close with X");
    auto last = host.table().now_ms();
    std::uint64_t frames = 0;
    while (!host.close_requested()) {
        const auto now = host.table().now_ms();
        std::uint64_t dt = now - last;
        last = now;
        dt = std::min<std::uint64_t>(dt, 100);
        panel.tick(dt);
        static_cast<void>(visor::ui::run_dirty(host.table(), panel, input, strip, kScreen, dirty));
        ++frames;
        if (frame_limit > 0 && frames >= frame_limit) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    return 0;
}
