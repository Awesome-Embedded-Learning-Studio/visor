// A monochrome control gallery rendered entirely by the portable UI core.
#include "demo_font.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/x11/x11_window_host.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/control_style.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/button/button.hpp"
#include "visor/ui/widgets/checkbox/checkbox.hpp"
#include "visor/ui/widgets/dropdown/dropdown.hpp"
#include "visor/ui/widgets/radio/radio.hpp"
#include "visor/ui/widgets/slider/slider.hpp"
#include "visor/ui/widgets/textbox/textbox.hpp"
#include "visor/ui/widgets/widget.hpp"

#include <array>
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
using visor::canvas::Ink;
using visor::examples::kDemoFont;
constexpr SizeI kScreen{.w = 480, .h = 320};
constexpr RectI kScreenBounds{.pos = {0, 0}, .size = kScreen};
constexpr visor::ui::Theme kPaper{
    .background = Ink::white(),
    .foreground = Ink::black(),
    .muted = Ink::black(),
    .accent = Ink::black(),
    .on_accent = Ink::white(),
    .border = Ink::black(),
    .disabled = Ink::black(),
    .corner_radius = {.top_left = 3, .top_right = 3, .bottom_right = 3, .bottom_left = 3},
};
constexpr visor::ui::Theme kNight{
    .background = Ink::black(),
    .foreground = Ink::white(),
    .muted = Ink::white(),
    .accent = Ink::white(),
    .on_accent = Ink::black(),
    .border = Ink::white(),
    .disabled = Ink::white(),
    .corner_radius = {.top_left = 3, .top_right = 3, .bottom_right = 3, .bottom_left = 3},
};

class Gallery final : public visor::ui::Widget {
  public:
    Gallery() : Widget{kScreenBounds} {
        set_theme(&kPaper);
        name_.set_text("VISOR-01");
        brightness_.set_value(brightness_.max() * 65 / 100);
        apply_.set_label("APPLY", kDemoFont);
        reset_.set_label("RESET", kDemoFont);
        reset_.set_outlined(true);
    }

    [[nodiscard]] bool initialize() noexcept {
        if (!appearance_.add_option("PAPER") || !appearance_.add_option("NIGHT") ||
            !output_.add_option("MONO / 1 BIT") || !output_.add_option("OLED / I2C") ||
            !output_.add_option("PC / X11")) {
            return false;
        }
        appearance_.select(0);
        output_.select(0);
        appearance_.set_on_change(
            [](visor::ui::RadioGroup& group, void* context) {
                auto& gallery = *static_cast<Gallery*>(context);
                gallery.set_theme(group.selected() == 0 ? &kPaper : &kNight);
            },
            this);
        apply_.set_on_activate(
            [](visor::ui::Button&, void* context) {
                auto& gallery = *static_cast<Gallery*>(context);
                gallery.status_ = "SETTINGS APPLIED.";
                gallery.invalidate(kScreenBounds);
            },
            this);
        reset_.set_on_activate(
            [](visor::ui::Button&, void* context) {
                auto& gallery = *static_cast<Gallery*>(context);
                gallery.name_.set_text("VISOR-01");
                gallery.grid_.set_checked(false);
                gallery.brightness_.set_value(gallery.brightness_.max() * 65 / 100);
                gallery.appearance_.select(0);
                gallery.output_.select(0);
                gallery.output_.set_open(false);
                gallery.status_ = "DEFAULTS RESTORED.";
                gallery.invalidate(kScreenBounds);
            },
            this);
        brightness_.set_on_value(
            [](visor::ui::Slider&, void* context) {
                static_cast<Gallery*>(context)->invalidate(kScreenBounds);
            },
            this);
        // The list is last so its expanded rows paint above the card content.
        return add_child(&name_) && add_child(&grid_) && add_child(&brightness_) &&
               add_child(&appearance_) && add_child(&reset_) && add_child(&apply_) &&
               add_child(&output_);
    }

  protected:
    void on_draw(const visor::ui::Painter& painter) const noexcept override {
        const auto fg = ColorRole::Foreground;
        const auto bg = ColorRole::Background;
        painter.fill(bounds(), bg);
        const auto text = [&](PointI at, std::string_view words, int scale = 1) {
            visor::ui::draw_text(painter, at, kDemoFont, words, fg, scale);
        };
        text({22, 20}, "VISOR", 3);
        text({24, 50}, "SMALL PIXELS. CLEAR INTENT.");
        text({342, 23}, "CONTROL STUDY / 01");
        text({342, 39}, "480 X 320 / 1 BPP");
        painter.fill(RectI{.pos = {22, 72}, .size = {.w = 436, .h = 1}}, fg);
        text({24, 87}, "DISPLAY SETTINGS");
        text({336, 87}, "BLACK. WHITE. DONE.");
        visor::ui::paint_outline(
            painter, {.pos = {22, 108}, .size = {.w = 278, .h = 154}}, ColorRole::Border,
            ColorRole::Background,
            {.top_left = 4, .top_right = 4, .bottom_right = 4, .bottom_left = 4});
        visor::ui::paint_outline(
            painter, {.pos = {312, 108}, .size = {.w = 146, .h = 154}}, ColorRole::Border,
            ColorRole::Background,
            {.top_left = 4, .top_right = 4, .bottom_right = 4, .bottom_left = 4});
        text({36, 121}, "DEVICE NAME");
        text({57, 181}, "ENABLE PIXEL GRID");
        text({36, 214}, "BRIGHTNESS");
        std::array<char, 8> percent{};
        const int percentage =
            ((brightness_.value() * 100) + (brightness_.max() / 2)) / brightness_.max();
        const int count = std::snprintf(percent.data(), percent.size(), "%d%%", percentage);
        if (count < 0) {
            percent[0] = '\0';
        }
        text({244, 234}, percent.data());
        text({326, 121}, "APPEARANCE");
        text({326, 197}, "OUTPUT");
        text({24, 287}, status_);
        painter.fill(RectI{.pos = {22, 310}, .size = {.w = 3, .h = 3}}, fg);
        text({32, 308}, "TAB TO EXPLORE / ARROWS TO ADJUST");
        text({380, 308}, "VISOR / 0.1");
    }

  private:
    visor::ui::TextBox name_{RectI{.pos = {36, 137}, .size = {.w = 250, .h = 23}}, kDemoFont};
    visor::ui::Checkbox grid_{RectI{.pos = {34, 176}, .size = {.w = 17, .h = 17}}};
    visor::ui::Slider brightness_{RectI{.pos = {36, 229}, .size = {.w = 192, .h = 17}}};
    visor::ui::RadioGroup appearance_{RectI{.pos = {324, 137}, .size = {.w = 120, .h = 30}},
                                      kDemoFont};
    visor::ui::Button reset_{RectI{.pos = {252, 277}, .size = {.w = 82, .h = 23}}};
    visor::ui::Button apply_{RectI{.pos = {346, 277}, .size = {.w = 112, .h = 23}}};
    visor::ui::Dropdown output_{RectI{.pos = {326, 212}, .size = {.w = 118, .h = 23}}, kDemoFont};
    std::string_view status_ = "READY WHEN YOU ARE.";
};
} // namespace

int main(int argc, char* const* argv) {
    Gallery panel;
    if (!panel.initialize()) {
        std::println(stderr, "visor: the gallery's child or option table is full");
        return 1;
    }
    using Frame = visor::canvas::Canvas<kScreen>;
    // This exact scene can also be inspected without an X server.
    if (argc == 3 && std::strcmp(argv[1], "--snapshot") == 0) {
        Frame frame;
        panel.draw(visor::ui::CanvasPainter{frame, *panel.theme()});
        auto* file = std::fopen(argv[2], "wb");
        if (file == nullptr) {
            return 1;
        }
        std::println(file, "P4\n{} {}", kScreen.w, kScreen.h);
        // PBM uses 1 for black; the canvas uses 1 for a lit pixel.
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
    panel.set_dirty_sink(&dirty);
    dirty.push(kScreenBounds);
    Frame frame;
    std::println("visor: Tab to navigate, arrows to adjust, Enter to activate; close with X");
    while (!host.close_requested()) {
        static_cast<void>(visor::ui::run_dirty(host.table(), panel, input, frame, kScreen, dirty));
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    return 0;
}
