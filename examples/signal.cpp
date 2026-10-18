// A live monochrome signal lab: an animated scope over real controls.
#include "demo_font.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/base/helpers/rect_helper.hpp"
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
#include "visor/ui/widgets/button/button.hpp"
#include "visor/ui/widgets/knob/knob.hpp"
#include "visor/ui/widgets/radio/radio.hpp"
#include "visor/ui/widgets/widget.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <thread>
#include <utility>

namespace {

namespace base = visor::base;
namespace draw = visor::draw;

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::canvas::Canvas;
using visor::canvas::ColorRole;
using visor::canvas::Ink;
using visor::examples::kDemoFont;
using visor::host::OffscreenHost;
using visor::host::PointerDown;
using visor::host::PointerMove;
using visor::host::PointerUp;
using visor::ui::Button;
using visor::ui::CanvasPainter;
using visor::ui::CornerRadius;
using visor::ui::DirtyRegions;
using visor::ui::InputRouter;
using visor::ui::Knob;
using visor::ui::Painter;
using visor::ui::RadioGroup;
using visor::ui::Theme;
using visor::ui::Widget;

constexpr SizeI kScreen{.w = 480, .h = 320};
using Frame = Canvas<kScreen>;
constexpr RectI kScreenBounds{.pos = {0, 0}, .size = kScreen};
constexpr RectI kHeaderRule{.pos = {18, 50}, .size = {.w = 444, .h = 1}};
constexpr RectI kScopeFrame{.pos = {16, 56}, .size = {.w = 308, .h = 184}};
constexpr RectI kScopeBounds{.pos = {18, 58}, .size = {.w = 304, .h = 180}};
constexpr RectI kModeBounds{.pos = {336, 78}, .size = {.w = 124, .h = 48}};
constexpr RectI kReadoutBounds{.pos = {336, 132}, .size = {.w = 124, .h = 72}};
constexpr RectI kStatusBounds{.pos = {420, 10}, .size = {.w = 44, .h = 32}};
constexpr RectI kRunBounds{.pos = {336, 248}, .size = {.w = 60, .h = 24}};
constexpr RectI kInvertBounds{.pos = {404, 248}, .size = {.w = 60, .h = 24}};
constexpr RectI kFreqBounds{.pos = {20, 250}, .size = {.w = 44, .h = 44}};
constexpr RectI kAmpBounds{.pos = {100, 250}, .size = {.w = 44, .h = 44}};
constexpr RectI kPhaseBounds{.pos = {180, 250}, .size = {.w = 44, .h = 44}};
constexpr RectI kGlowBounds{.pos = {260, 250}, .size = {.w = 44, .h = 44}};
constexpr RectI kFooterRule{.pos = {18, 300}, .size = {.w = 444, .h = 1}};

// The instrument's two schemes: night is the default cold panel, paper is the
// old recorder's sheet. The corner radius travels with the scheme.
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

/// The sine table's unit: one unit of sine is 1/4096 of a full swing.
constexpr int kSineScale = 4096;

// Bhaskara I's sine approximation in fixed point, the same integer-only
// recursion the Knob control uses: for d in [0, 180],
// sin(d) ~= 4d(180-d) / (40500 - d(180-d)).
constexpr int sine_degrees(int degrees) noexcept {
    int d = degrees % 360;
    if (d < 0) {
        d += 360;
    }
    int sign = 1;
    if (d >= 180) {
        d -= 180;
        sign = -1;
    }
    if (d > 90) {
        d = 180 - d;
    }
    const int product = d * (180 - d);
    return sign * ((4 * product * kSineScale) / (40500 - product));
}

constexpr std::array<std::int16_t, 256> make_sine_table() noexcept {
    std::array<std::int16_t, 256> table{};
    for (int i = 0; i < 256; ++i) {
        table[static_cast<std::size_t>(i)] =
            static_cast<std::int16_t>(sine_degrees((i * 360) / 256));
    }
    return table;
}

inline constexpr auto kSineTable = make_sine_table();

// A two-pixel Bresenham stroke, the scope's handwriting for curves whose
// sample points would otherwise leave gaps. The Knob carries the same brush
// for its pointer; the two live in different translation units on purpose,
// the same stance the test doubles take.
void plot_segment(const Painter& painter, PointI from, PointI to, ColorRole ink) noexcept {
    int x = from.x;
    int y = from.y;
    int dx = to.x - x;
    int dy = to.y - y;
    const int step_x = dx < 0 ? -1 : 1;
    const int step_y = dy < 0 ? -1 : 1;
    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    const auto brush = [&painter, ink](PointI at) {
        painter.plot(at, ink);
        painter.plot(PointI{at.x + 1, at.y}, ink);
        painter.plot(PointI{at.x, at.y + 1}, ink);
        painter.plot(PointI{at.x + 1, at.y + 1}, ink);
    };
    int error = dx - dy;
    while (true) {
        brush(PointI{x, y});
        if (x == to.x && y == to.y) {
            return;
        }
        const int twice = 2 * error;
        if (twice > -dy) {
            error -= dy;
            x += step_x;
        }
        if (twice < dx) {
            error += dx;
            y += step_y;
        }
    }
}

/**
 * @brief The animated plot: a 304x180 window that owns the phase and the
 *        mode-specific shape, and nothing else.
 *
 * The replay contract splits the work cleanly: on_draw reads phase, settings
 * and the spectrum's held peaks and paints; tick() is the only writer, run by
 * the panel's frame loop. A strip replay therefore paints the same shape at
 * every strip, which is why the trace is a pure function of the state.
 */
class Scope final : public Widget {
  public:
    enum class Mode : std::uint8_t { Xy, Yt, Spectrum };

    explicit Scope(base::RectI bounds) noexcept : Widget{bounds} {}

    void set_mode(Mode mode) noexcept;
    void set_frequency(int value) noexcept;
    void set_amplitude(int value) noexcept;
    void set_phase_offset(int value) noexcept;
    void set_glow(int value) noexcept;
    void set_running(bool on) noexcept;

    /// Advances the animation by dt_ms. A held scope writes nothing and owes
    /// no pixels; the released one owes its whole plot, and only its plot.
    void tick(std::uint64_t dt_ms) noexcept;

  protected:
    void on_draw(const Painter& painter) const noexcept override;

  private:
    static constexpr int kBars = 24;
    static constexpr int kSamples = 1600;
    static constexpr int kPhaseMask = 4095;
    static constexpr int kPeakDecay = 24;

    [[nodiscard]] static int sine_at(int pos) noexcept;
    void draw_grid(const Painter& painter, ColorRole fg) const noexcept;
    void draw_xy(const Painter& painter, ColorRole fg) const noexcept;
    void draw_yt(const Painter& painter, ColorRole fg) const noexcept;
    void draw_spectrum(const Painter& painter, ColorRole fg) const noexcept;
    void update_spectrum() noexcept;

    Mode mode_ = Mode::Xy;
    bool running_ = true;
    int frequency_ = 4;
    int amplitude_ = 78;
    int phase_offset_ = 120;
    int glow_ = 2;
    std::uint32_t phase_ = 0;
    std::array<int, kBars> levels_{};
    std::array<int, kBars> peaks_{};
};

int Scope::sine_at(int pos) noexcept {
    // The +kSineScale keeps a ghost's negative phase in the table's positive
    // index space; the mask is the one wrap every caller shares.
    const auto wrapped =
        static_cast<std::uint32_t>(pos + kSineScale) & static_cast<std::uint32_t>(kPhaseMask);
    const auto index = static_cast<std::size_t>(wrapped >> 4U);
    const int fraction = static_cast<int>(wrapped & 15U);
    const int first = kSineTable[index];
    const int second = kSineTable[(index + 1) % kSineTable.size()];
    return ((first * (16 - fraction)) + (second * fraction)) / 16;
}

void Scope::set_mode(Mode mode) noexcept {
    if (mode_ == mode) {
        return;
    }
    mode_ = mode;
    if (mode_ == Mode::Spectrum) {
        update_spectrum();
    }
    invalidate(bounds());
}

void Scope::set_frequency(int value) noexcept {
    const int clamped = std::clamp(value, 1, 8);
    if (clamped == frequency_) {
        return;
    }
    frequency_ = clamped;
    invalidate(bounds());
}

void Scope::set_amplitude(int value) noexcept {
    const int clamped = std::clamp(value, 0, 100);
    if (clamped == amplitude_) {
        return;
    }
    amplitude_ = clamped;
    invalidate(bounds());
}

void Scope::set_phase_offset(int value) noexcept {
    const int clamped = std::clamp(value, 0, 359);
    if (clamped == phase_offset_) {
        return;
    }
    phase_offset_ = clamped;
    invalidate(bounds());
}

void Scope::set_glow(int value) noexcept {
    const int clamped = std::clamp(value, 0, 5);
    if (clamped == glow_) {
        return;
    }
    glow_ = clamped;
    invalidate(bounds());
}

void Scope::set_running(bool on) noexcept {
    // Freezing asks for no repaint: the last frame is already the picture of
    // a held trace. Resuming repaints on the next tick.
    running_ = on;
}

void Scope::tick(std::uint64_t dt_ms) noexcept {
    if (!running_) {
        return;
    }
    // A stalled frame must not teleport the phase; the cap is the same
    // judgement the main loop makes for its own dt.
    const std::uint64_t capped = std::min<std::uint64_t>(dt_ms, 100);
    const auto advance =
        static_cast<std::uint32_t>(capped * static_cast<std::uint64_t>(1 + frequency_));
    phase_ = (phase_ + advance) & static_cast<std::uint32_t>(kPhaseMask);
    if (mode_ == Mode::Spectrum) {
        update_spectrum();
    }
    invalidate(bounds());
}

void Scope::update_spectrum() noexcept {
    for (int i = 0; i < kBars; ++i) {
        const int first = sine_at(static_cast<int>(phase_) + (i * 256));
        const int second = sine_at((static_cast<int>(phase_) * 3) + (i * 511));
        // Two waves plus a pedestal, mapped back onto 0..kSineScale.
        const int sum = first + second + (2 * kSineScale);
        const int level = (sum * kSineScale) / (4 * kSineScale);
        levels_[static_cast<std::size_t>(i)] = level;
        // Peak hold: the marker rides the highest recent level, then falls.
        const int decayed = peaks_[static_cast<std::size_t>(i)] - kPeakDecay;
        peaks_[static_cast<std::size_t>(i)] = std::max(level, decayed > 0 ? decayed : 0);
    }
}

void Scope::on_draw(const Painter& painter) const noexcept {
    if (bounds().is_empty()) {
        return;
    }
    const auto fg = ColorRole::Foreground;
    const auto bg = ColorRole::Background;
    painter.fill(bounds(), bg);
    draw_grid(painter, fg);
    switch (mode_) {
        case Mode::Xy:
            draw_xy(painter, fg);
            break;
        case Mode::Yt:
            draw_yt(painter, fg);
            break;
        case Mode::Spectrum:
            draw_spectrum(painter, fg);
            break;
    }
}

void Scope::draw_grid(const Painter& painter, ColorRole fg) const noexcept {
    const int x0 = bounds().pos.x;
    const int y0 = bounds().pos.y;
    const int w = bounds().size.w;
    const int h = bounds().size.h;
    // Eight columns and six rows, the centre lines denser: a dotted graticule
    // that leaves the trace the only solid stroke in the plot.
    for (int i = 1; i < 8; ++i) {
        const int x = x0 + ((w * i) / 8);
        const int step = i == 4 ? 2 : 4;
        for (int y = y0; y < y0 + h; y += step) {
            painter.plot(PointI{x, y}, fg);
        }
    }
    for (int i = 1; i < 6; ++i) {
        const int y = y0 + ((h * i) / 6);
        const int step = i == 3 ? 2 : 4;
        for (int x = x0; x < x0 + w; x += step) {
            painter.plot(PointI{x, y}, fg);
        }
    }
}

void Scope::draw_xy(const Painter& painter, ColorRole fg) const noexcept {
    const int cx = bounds().pos.x + (bounds().size.w / 2);
    const int cy = bounds().pos.y + (bounds().size.h / 2);
    const int rx = (bounds().size.w / 2) - 6;
    const int ry = (bounds().size.h / 2) - 6;
    const int amplitude = (amplitude_ * kSineScale) / 100;
    const int frequency_x = 1 + frequency_;
    const int frequency_y = 2 + frequency_;
    const int offset = (phase_offset_ * kSineScale) / 360;
    for (int ghost = glow_; ghost >= 0; --ghost) {
        const int ghost_phase = static_cast<int>(phase_) - (ghost * 160);
        const bool thick = ghost == 0;
        const int step = thick ? 1 : (ghost + 1);
        int previous_x = 0;
        int previous_y = 0;
        bool has_previous = false;
        for (int sample = 0; sample <= kSamples; sample += step) {
            const int t = (sample * kSineScale) / kSamples;
            const int sx = sine_at((frequency_x * t) + ghost_phase + offset);
            const int sy = sine_at((frequency_y * t) + ghost_phase);
            const int x = cx + ((rx * ((amplitude * sx) / kSineScale)) / kSineScale);
            const int y = cy + ((ry * ((amplitude * sy) / kSineScale)) / kSineScale);
            if (!has_previous) {
                if (thick) {
                    painter.fill(RectI{.pos = {x - 1, y - 1}, .size = {.w = 2, .h = 2}}, fg);
                } else {
                    painter.plot(PointI{x, y}, fg);
                }
            } else if (thick) {
                plot_segment(painter, PointI{previous_x, previous_y}, PointI{x, y}, fg);
            } else {
                painter.plot(PointI{x, y}, fg);
            }
            previous_x = x;
            previous_y = y;
            has_previous = true;
        }
    }
}

void Scope::draw_yt(const Painter& painter, ColorRole fg) const noexcept {
    const int x0 = bounds().pos.x;
    const int cy = bounds().pos.y + (bounds().size.h / 2);
    const int w = bounds().size.w;
    const int amplitude = (amplitude_ * ((bounds().size.h / 2) - 6)) / 100;
    const int offset = (phase_offset_ * kSineScale) / 360;
    const int cycles = frequency_;
    for (int ghost = glow_; ghost >= 0; --ghost) {
        const int ghost_phase = static_cast<int>(phase_) - (ghost * 160);
        const bool thick = ghost == 0;
        const int step = thick ? 1 : (ghost + 1);
        int previous_y = cy;
        for (int x = 0; x < w; x += step) {
            const int pos = ghost_phase + offset + ((x * cycles * kSineScale) / w);
            const int y = cy - ((amplitude * sine_at(pos)) / kSineScale);
            if (!thick) {
                painter.plot(PointI{x0 + x, y}, fg);
            } else if (x == 0) {
                painter.fill(RectI{.pos = {x0, y - 1}, .size = {.w = 2, .h = 2}}, fg);
            } else {
                // One column of the sweep: a vertical span joins the two
                // neighbouring samples, so a steep edge stays continuous.
                const int top = std::min(previous_y, y);
                const int bottom = std::max(previous_y, y);
                painter.fill(RectI{.pos = {x0 + x, top}, .size = {.w = 2, .h = bottom - top + 1}},
                             fg);
            }
            previous_y = y;
        }
    }
}

void Scope::draw_spectrum(const Painter& painter, ColorRole fg) const noexcept {
    const int x0 = bounds().pos.x;
    const int y0 = bounds().pos.y;
    const int w = bounds().size.w;
    const int h = bounds().size.h;
    const int gap = 2;
    const int bar_width = (w - ((kBars - 1) * gap)) / kBars;
    if (bar_width <= 0) {
        return;
    }
    const int field = (bar_width * kBars) + ((kBars - 1) * gap);
    const int start = x0 + ((w - field) / 2);
    const int base = y0 + h - 1;
    const int usable = h - 10;
    for (int i = 0; i < kBars; ++i) {
        const int x = start + (i * (bar_width + gap));
        const int level = (levels_[static_cast<std::size_t>(i)] * usable) / kSineScale;
        if (level > 0) {
            painter.fill(RectI{.pos = {x, base - level}, .size = {.w = bar_width, .h = level}}, fg);
        }
        const int peak = (peaks_[static_cast<std::size_t>(i)] * usable) / kSineScale;
        if (peak > 0) {
            painter.fill(RectI{.pos = {x, base - peak}, .size = {.w = bar_width, .h = 2}}, fg);
        }
    }
    painter.fill(RectI{.pos = {x0, base}, .size = {.w = w, .h = 1}}, fg);
}

/**
 * @brief The instrument panel: header, plot, readouts, knobs and buttons.
 *
 * The root owns the frame text and the parameter readouts, the Scope child
 * owns the animated plot, and the stock controls own their own pixels. The
 * tick path is the chapter's whole point: advance the Scope, let it report
 * its bounds, repaint only the strips that touch them.
 */
class SignalPanel final : public Widget {
  public:
    SignalPanel();

    [[nodiscard]] bool initialize() noexcept;
    void tick(std::uint64_t dt_ms) noexcept;
    void advance_for(std::uint64_t ms) noexcept;
    void set_demo(bool on) noexcept;
    void set_running(bool on) noexcept;
    [[nodiscard]] bool self_test() noexcept;

  protected:
    void on_draw(const Painter& painter) const noexcept override;

  private:
    void advance_demo(std::uint64_t dt_ms) noexcept;

    Scope scope_{kScopeBounds};
    RadioGroup mode_{kModeBounds, kDemoFont};
    Knob frequency_{kFreqBounds, 1, 8, 4};
    Knob amplitude_{kAmpBounds, 0, 100, 78};
    Knob phase_{kPhaseBounds, 0, 359, 120};
    Knob glow_{kGlowBounds, 0, 5, 2};
    Button run_{kRunBounds};
    Button invert_{kInvertBounds};
    bool running_ = true;
    bool demo_ = false;
    std::uint64_t demo_ms_ = 0;
};

SignalPanel::SignalPanel() : Widget{kScreenBounds} {
    set_theme(&kNight);
    // Start the Scope on the same numbers the knobs show; a tree wired later
    // takes its first frame as one whole-screen debt anyway.
    scope_.set_frequency(frequency_.value());
    scope_.set_amplitude(amplitude_.value());
    scope_.set_phase_offset(phase_.value());
    scope_.set_glow(glow_.value());
    run_.set_label("HOLD", kDemoFont);
    run_.set_outlined(true);
    invert_.set_label("INVERT", kDemoFont);
    invert_.set_outlined(true);
}

bool SignalPanel::initialize() noexcept {
    if (!mode_.add_option("XY") || !mode_.add_option("YT") || !mode_.add_option("SPEC")) {
        return false;
    }
    mode_.set_on_change(
        [](RadioGroup& group, void* context) {
            auto& panel = *static_cast<SignalPanel*>(context);
            if (group.selected() == 1) {
                panel.scope_.set_mode(Scope::Mode::Yt);
            } else if (group.selected() == 2) {
                panel.scope_.set_mode(Scope::Mode::Spectrum);
            } else {
                panel.scope_.set_mode(Scope::Mode::Xy);
            }
            panel.invalidate(kReadoutBounds);
        },
        this);
    mode_.select(0);
    frequency_.set_on_value(
        [](Knob& knob, void* context) {
            auto& panel = *static_cast<SignalPanel*>(context);
            panel.scope_.set_frequency(knob.value());
            panel.invalidate(kReadoutBounds);
        },
        this);
    amplitude_.set_on_value(
        [](Knob& knob, void* context) {
            auto& panel = *static_cast<SignalPanel*>(context);
            panel.scope_.set_amplitude(knob.value());
            panel.invalidate(kReadoutBounds);
        },
        this);
    phase_.set_on_value(
        [](Knob& knob, void* context) {
            auto& panel = *static_cast<SignalPanel*>(context);
            panel.scope_.set_phase_offset(knob.value());
            panel.invalidate(kReadoutBounds);
        },
        this);
    glow_.set_on_value(
        [](Knob& knob, void* context) {
            auto& panel = *static_cast<SignalPanel*>(context);
            panel.scope_.set_glow(knob.value());
            panel.invalidate(kReadoutBounds);
        },
        this);
    run_.set_on_activate(
        [](Button&, void* context) {
            auto& panel = *static_cast<SignalPanel*>(context);
            panel.set_running(!panel.running_);
        },
        this);
    invert_.set_on_activate(
        [](Button&, void* context) {
            auto& panel = *static_cast<SignalPanel*>(context);
            panel.set_theme(panel.theme() == &kNight ? &kPaper : &kNight);
        },
        this);
    return add_child(&scope_) && add_child(&mode_) && add_child(&frequency_) &&
           add_child(&amplitude_) && add_child(&phase_) && add_child(&glow_) && add_child(&run_) &&
           add_child(&invert_);
}

void SignalPanel::tick(std::uint64_t dt_ms) noexcept {
    if (demo_) {
        advance_demo(dt_ms);
    }
    scope_.tick(dt_ms);
}

void SignalPanel::advance_for(std::uint64_t ms) noexcept {
    // The snapshot's clock: fixed 16 ms beats, so a given --at-ms produces the
    // same picture on every machine and every run.
    constexpr std::uint64_t step_size = 16;
    while (ms > 0) {
        const std::uint64_t step = ms > step_size ? step_size : ms;
        tick(step);
        ms -= step;
    }
}

void SignalPanel::set_demo(bool on) noexcept {
    if (demo_ == on) {
        return;
    }
    demo_ = on;
    demo_ms_ = 0;
}

void SignalPanel::set_running(bool on) noexcept {
    if (running_ == on) {
        return;
    }
    running_ = on;
    scope_.set_running(on);
    // The button states the action, the header states the condition.
    run_.set_label(on ? "HOLD" : "RUN", kDemoFont);
    invalidate(kStatusBounds);
}

void SignalPanel::advance_demo(std::uint64_t dt_ms) noexcept {
    demo_ms_ += dt_ms;
    const int wanted_mode = static_cast<int>((demo_ms_ / 4000) % 3);
    if (wanted_mode != mode_.selected()) {
        mode_.select(wanted_mode);
    }
    frequency_.set_value(1 + static_cast<int>((demo_ms_ / 1200) % 8));
    phase_.set_value(static_cast<int>((demo_ms_ / 20) % 360));
}

void SignalPanel::on_draw(const Painter& painter) const noexcept {
    const auto fg = ColorRole::Foreground;
    const auto bg = ColorRole::Background;
    painter.fill(bounds(), bg);

    draw_text(painter, {18, 12}, kDemoFont, "VISOR // SIGNAL", fg, 2);
    draw_text(painter, {200, 34}, kDemoFont, "MONO SIGNAL LAB / 01", fg, 1);
    draw_text(painter, {340, 16}, kDemoFont, "480X320 / 1BPP", fg, 1);

    const char* state = running_ ? "RUN" : "HOLD";
    const SizeI state_size = draw::measure_text(kDemoFont, state, 1);
    draw_text(painter, {458 - state_size.w, 30}, kDemoFont, state, fg, 1);
    const RectI led{.pos = {448, 16}, .size = {.w = 5, .h = 5}};
    paint_outline(painter, led, fg, bg);
    if (running_) {
        painter.fill(base::inset(led, 1), fg);
    }

    painter.fill(kHeaderRule, fg);
    paint_outline(painter, kScopeFrame, fg, bg,
                  CornerRadius{.top_left = 4, .top_right = 4, .bottom_right = 4, .bottom_left = 4});
    draw_text(painter, {336, 64}, kDemoFont, "MODE", fg, 1);

    std::array<char, 32> text{};
    const auto value_line = [&](int y, const char* format, int value) {
        const int count = std::snprintf(text.data(), text.size(), format, value);
        if (count > 0) {
            draw_text(painter, {336, y}, kDemoFont, text.data(), fg, 1);
        }
    };
    value_line(136, "FREQ  %d", frequency_.value());
    value_line(150, "AMP   %d", amplitude_.value());
    value_line(164, "PHASE %d", phase_.value());
    value_line(178, "GLOW  %d", glow_.value());
    const char* mode_name = "XY";
    if (mode_.selected() == 1) {
        mode_name = "YT";
    } else if (mode_.selected() == 2) {
        mode_name = "SPEC";
    }
    const int mode_count = std::snprintf(text.data(), text.size(), "MODE  %s", mode_name);
    if (mode_count > 0) {
        draw_text(painter, {336, 192}, kDemoFont, text.data(), fg, 1);
    }

    // The knob captions are the panel's text, not child widgets: the knobs
    // themselves are the only children the panel has room for.
    const auto knob_label = [&](const RectI& box, const char* words) {
        const SizeI size = draw::measure_text(kDemoFont, words, 1);
        draw_text(painter, {box.pos.x + ((box.size.w - size.w) / 2), 242}, kDemoFont, words, fg, 1);
    };
    knob_label(kFreqBounds, "FREQ");
    knob_label(kAmpBounds, "AMP");
    knob_label(kPhaseBounds, "PHASE");
    knob_label(kGlowBounds, "GLOW");

    painter.fill(kFooterRule, fg);
    draw_text(painter, {18, 306}, kDemoFont, "TAB TO FOCUS / TURN KNOBS / ENTER TO ACT", fg, 1);
    draw_text(painter, {396, 306}, kDemoFont, "VISOR / 0.1", fg, 1);
}

bool SignalPanel::self_test() noexcept {
    const auto require = [](bool ok, const char* what) {
        if (!ok) {
            std::println(stderr, "signal self-test: {}", what);
        }
        return ok;
    };

    tick(0);
    Frame full;
    full.set_clear(false);
    draw(CanvasPainter{full, *theme()});

    // The replay gate: the whole tree drawn once must equal the same tree
    // replayed through 16-row strips, every pixel.
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

    // A running tick owes the plot and nothing else. The header and the
    // readout panel live in strips the plot's bounds never touch.
    DirtyRegions debts;
    set_dirty_sink(&debts);
    debts.clear();
    tick(50);
    if (!require(debts.touches(kScopeBounds), "a running tick does not owe the plot")) {
        return false;
    }
    if (!require(!debts.touches(kHeaderRule), "a running tick repaints the header")) {
        return false;
    }
    if (!require(!debts.touches(kReadoutBounds), "a running tick repaints the readouts")) {
        return false;
    }

    // Holding freezes the phase, and a frozen frame owes zero pixels: the
    // economy path's honest floor is a submit count of zero.
    set_running(false);
    debts.clear();
    tick(50);
    if (!require(debts.count() == 0, "a held tick owes pixels")) {
        return false;
    }
    set_running(true);
    debts.clear();

    // The event loop: a press on the knob's ring, a captured quarter-turn to
    // the right, a release. The plot must change, which proves event -> knob
    // -> scope -> pixels without reaching into the panel's private state.
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

    const PointI center{
        kFreqBounds.pos.x + (kFreqBounds.size.w / 2),
        kFreqBounds.pos.y + (kFreqBounds.size.h / 2),
    };
    const PointI grab{center.x, center.y - 10};   // north of the pivot
    const PointI turned{center.x + 10, center.y}; // a quarter-turn clockwise
    if (!require(screen.push_event(PointerDown{.button = 1, .pos = grab}),
                 "the event feed refused the press")) {
        return false;
    }
    static_cast<void>(
        visor::ui::run_dirty(screen.table(), *this, input, event_strip, kScreen, event_debts));
    if (!require(screen.push_event(PointerMove{.pos = turned}),
                 "the event feed refused the move")) {
        return false;
    }
    static_cast<void>(
        visor::ui::run_dirty(screen.table(), *this, input, event_strip, kScreen, event_debts));
    if (!require(screen.push_event(PointerUp{.button = 1, .pos = turned}),
                 "the event feed refused the release")) {
        return false;
    }
    static_cast<void>(
        visor::ui::run_dirty(screen.table(), *this, input, event_strip, kScreen, event_debts));

    int changed = 0;
    for (int y = kScopeBounds.pos.y; y < kScopeBounds.pos.y + kScopeBounds.size.h; ++y) {
        for (int x = kScopeBounds.pos.x; x < kScopeBounds.pos.x + kScopeBounds.size.w; ++x) {
            if (screen.get(PointI{x, y}) != before.get(PointI{x, y})) {
                ++changed;
            }
        }
    }
    if (!require(changed > 0, "turning the frequency knob left the plot unchanged")) {
        return false;
    }
    std::println("signal self-test: ok ({} plot pixels changed by the turn)", changed);
    return true;
}

bool save_snapshot(const char* path, const Frame& frame) noexcept {
    auto* file = std::fopen(path, "wb");
    if (file == nullptr) {
        return false;
    }
    std::println(file, "P4\n{} {}", kScreen.w, kScreen.h);
    // PBM uses 1 for black; the canvas uses 1 for a lit pixel.
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

    SignalPanel panel;
    if (!panel.initialize()) {
        std::println(stderr, "visor: the signal panel's option or child table is full");
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

    std::println("visor: signal lab; turn the knobs, Tab to focus, Enter to act; close with X");
    auto last = host.table().now_ms();
    std::uint64_t frames = 0;
    while (!host.close_requested()) {
        const auto now = host.table().now_ms();
        std::uint64_t dt = now - last;
        last = now;
        dt = std::min<std::uint64_t>(dt, 100); // a paused window must not teleport the phase
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
