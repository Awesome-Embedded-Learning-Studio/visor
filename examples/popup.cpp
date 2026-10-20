// A dropdown whose open list lives on a full-screen popup layer: the list
// hangs past its window's edge, and a press anywhere outside dismisses it.
#include "demo_font.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/x11/x11_window_host.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/color.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/control_style.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/popup_layer.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/dropdown/dropdown.hpp"
#include "visor/ui/widgets/widget.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <print>
#include <string_view>
#include <thread>
#include <utility>

namespace {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::canvas::ColorRole;
using visor::examples::kDemoFont;

constexpr SizeI kScreen{.w = 480, .h = 320};
constexpr RectI kScreenBounds{.pos = {0, 0}, .size = kScreen};
constexpr RectI kPanelBounds{.pos = {40, 40}, .size = {.w = 300, .h = 160}};
constexpr RectI kDropdownBounds{.pos = {60, 170}, .size = {.w = 180, .h = 23}};

/// A window-like card: an outline and two lines of text, no input of its own.
class Panel final : public visor::ui::Widget {
  public:
    using Widget::Widget;

  protected:
    void on_draw(const visor::ui::Painter& painter) const noexcept override {
        painter.fill(bounds(), ColorRole::Background);
        visor::ui::paint_outline(
            painter, bounds(), ColorRole::Border, ColorRole::Background,
            {.top_left = 4, .top_right = 4, .bottom_right = 4, .bottom_left = 4});
        visor::ui::draw_text(painter, {bounds().pos.x + 12, bounds().pos.y + 12}, kDemoFont,
                             "OUTPUT", ColorRole::Foreground, 1);
        visor::ui::draw_text(painter, {bounds().pos.x + 12, bounds().pos.y + 28}, kDemoFont,
                             "THE LIST MAY LEAVE THIS CARD", ColorRole::Muted, 1);
    }
};

class PopupDemo final : public visor::ui::Widget {
  public:
    PopupDemo() : Widget{kScreenBounds} {
        static_cast<void>(add_child(&layer_));
        static_cast<void>(add_child(&panel_));
        static_cast<void>(panel_.add_child(&dropdown_));
        dropdown_.set_popup(&layer_);
    }

    [[nodiscard]] bool initialize() noexcept {
        if (!dropdown_.add_option("PAPER") || !dropdown_.add_option("NIGHT") ||
            !dropdown_.add_option("OLED") || !dropdown_.add_option("X11")) {
            return false;
        }
        dropdown_.select(0);
        return true;
    }

    /// Headless judgment: open, dismiss outside, pick by keyboard, pick by
    /// pointer, and the dropdown's own bounds stay the closed row throughout.
    [[nodiscard]] bool self_test() noexcept {
        const RectI closed = dropdown_.bounds();
        dropdown_.set_open(true);
        if (!dropdown_.open() || !layer_.is_open() || layer_.content() == nullptr) {
            return false;
        }
        if (layer_.content()->bounds() == closed) {
            return false; // the list is a separate widget below the row
        }

        visor::ui::InputRouter input;
        press(input, {10, 300});
        if (dropdown_.open() || layer_.is_open()) {
            return false;
        }

        dropdown_.set_open(true);
        dropdown_.on_key_down(visor::host::kKeyDown, {});
        dropdown_.on_key_down(visor::host::kKeyEnter, {});
        if (dropdown_.open() || dropdown_.selected() != 1) {
            return false;
        }

        dropdown_.set_open(true);
        const int row_h = dropdown_.bounds().size.h;
        const PointI second_row{
            70,
            dropdown_.bounds().pos.y + (3 * row_h) + (row_h / 2),
        };
        press(input, second_row);
        if (dropdown_.open() || dropdown_.selected() != 2) {
            return false;
        }
        return dropdown_.bounds() == closed;
    }

    [[nodiscard]] bool popup_open() const noexcept { return dropdown_.open(); }

    void open_popup() noexcept { dropdown_.set_open(true); }

  protected:
    void on_draw(const visor::ui::Painter& painter) const noexcept override {
        painter.fill(bounds(), ColorRole::Background);
        visor::ui::draw_text(painter, {24, 20}, kDemoFont, "POPUP LAYER", ColorRole::Foreground, 2);
        visor::ui::draw_text(painter, {24, 300}, kDemoFont,
                             "CLICK THE FIELD / CLICK OUTSIDE TO DISMISS", ColorRole::Muted, 1);
        visor::ui::draw_text(painter, {372, 300}, kDemoFont, "VISOR / 0.1", ColorRole::Muted, 1);
    }

  private:
    void press(visor::ui::InputRouter& input, PointI at) noexcept {
        visor::ui::dispatch_event(
            *this, input, visor::host::Event{visor::host::PointerDown{.button = 1, .pos = at}}, 0,
            true);
        visor::ui::dispatch_event(
            *this, input, visor::host::Event{visor::host::PointerUp{.button = 1, .pos = at}}, 0,
            true);
    }

    // Declaration order is destruction order reversed: the dropdown goes
    // before the panel, and both before the layer their list lives on.
    visor::ui::PopupLayer layer_{kScreenBounds};
    Panel panel_{kPanelBounds};
    visor::ui::Dropdown dropdown_{kDropdownBounds, kDemoFont};
};

} // namespace

int main(int argc, char* const* argv) {
    PopupDemo demo;
    if (!demo.initialize()) {
        std::println(stderr, "visor: the dropdown's option table is full");
        return 1;
    }

    if (argc == 2 && std::strcmp(argv[1], "--self-test") == 0) {
        if (!demo.self_test()) {
            std::println(stderr, "popup self-test: failed");
            return 1;
        }
        std::println("popup self-test: ok (outside dismiss, keyboard pick, pointer pick)");
        return 0;
    }

    if (argc == 3 && std::strcmp(argv[1], "--snapshot") == 0) {
        using Frame = visor::canvas::Canvas<kScreen>;
        Frame frame;
        demo.open_popup();
        const visor::ui::Theme& scheme =
            demo.theme() != nullptr ? *demo.theme() : visor::ui::kLightTheme;
        demo.draw(visor::ui::CanvasPainter{frame, scheme});
        auto* file = std::fopen(argv[2], "wb");
        if (file == nullptr) {
            return 1;
        }
        std::println(file, "P4\n{} {}", kScreen.w, kScreen.h);
        for (const auto byte : frame.pixels()) {
            const auto inverted = static_cast<unsigned char>(255 - byte);
            if (std::fwrite(&inverted, 1, 1, file) != 1) {
                static_cast<void>(std::fclose(file));
                return 1;
            }
        }
        return std::fclose(file) == 0 ? 0 : 1;
    }

    auto opened = visor::host::x11::X11WindowHost::open(nullptr, kScreen.w, kScreen.h, 2);
    if (!opened.has_value()) {
        std::println(stderr, "visor: cannot open the display (is DISPLAY set?)");
        return 1;
    }
    auto host = std::move(*opened);
    visor::ui::InputRouter input;
    visor::ui::DirtyRegions dirty;
    demo.set_dirty_sink(&dirty);
    dirty.push(kScreenBounds);
    using Frame = visor::canvas::Canvas<kScreen>;
    Frame frame;
    std::println("visor: click the field to open, click outside to dismiss; close with X");
    while (!host.close_requested()) {
        static_cast<void>(visor::ui::run_dirty(host.table(), demo, input, frame, kScreen, dirty));
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    return 0;
}
