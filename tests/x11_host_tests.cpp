#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"
#include "visor/bindings/x11/x11_window_host.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/pixel_traits.hpp"
#include "visor/draw/pen.hpp"

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <fcntl.h>
#include <signal.h> // NOLINT(modernize-deprecated-headers): POSIX kill lives here, not in <csignal>
#include <sys/types.h>
#include <unistd.h>

#include <sys/wait.h>

#include <cstddef>
#include <cstdint>
#include <variant>

namespace {

constexpr int kWidth = 128;
constexpr int kHeight = 64;

/// Starts a private Xvfb on :99 unless one already answers, and stops only
/// what it started. A display that was already up is somebody else's.
class VirtualDisplay {
  public:
    explicit VirtualDisplay(const char* name) {
        display_ = XOpenDisplay(name);
        if (display_ != nullptr) {
            return;
        }
        const ::pid_t child = ::fork();
        if (child == 0) {
            // Xvfb chats on its stderr; the judgment output stays ours.
            const int null_fd = ::open("/dev/null", O_WRONLY);
            if (null_fd >= 0) {
                ::dup2(null_fd, 1);
                ::dup2(null_fd, 2);
            }
            ::execlp("Xvfb", "Xvfb", name, "-screen", "0", "640x480x24", nullptr);
            ::_exit(127);
        }
        spawned_ = child;
        for (int waited_ds = 0; waited_ds < 15 && display_ == nullptr; ++waited_ds) {
            ::usleep(100000);
            display_ = XOpenDisplay(name);
        }
    }

    ~VirtualDisplay() {
        if (display_ != nullptr) {
            XCloseDisplay(display_);
        }
        if (spawned_ > 0) {
            ::kill(spawned_, SIGTERM);
            int status = 0;
            ::waitpid(spawned_, &status, 0);
        }
    }

    [[nodiscard]] bool up() const { return display_ != nullptr; }
    [[nodiscard]] Display* display() const { return display_; }

  private:
    Display* display_ = nullptr;
    ::pid_t spawned_ = -1;
};

/// The host's promise read back from the server: every canvas bit lands as
/// the visual's own lit word or the dark word, nothing in between. scale is
/// the host's zoom: each canvas pixel must appear as a scale-by-scale block,
/// so every window pixel compares against the canvas pixel it comes from.
/// Fails loudly on an exotic depth instead of miscomparing.
template <typename CanvasT>
void compare_readback(Display* display, Window window, const CanvasT& canvas, int scale = 1) {
    XImage* shot = XGetImage(display, window, 0, 0, CanvasT::kWidth * scale,
                             CanvasT::kHeight * scale, AllPlanes, ZPixmap);
    VISOR_CHECK(shot != nullptr);
    if (shot == nullptr) {
        return;
    }
    VISOR_CHECK_EQ(shot->bits_per_pixel, 32);
    const std::uint32_t lit =
        shot->red_mask | shot->green_mask | shot->blue_mask; // the ink word, by masks
    const auto* words = reinterpret_cast<const std::uint32_t*>(shot->data);
    for (int y = 0; y < static_cast<int>(CanvasT::kHeight) * scale; ++y) {
        const std::uint32_t* row = words + (static_cast<std::size_t>(y) *
                                            static_cast<std::size_t>(shot->bytes_per_line / 4));
        for (int x = 0; x < static_cast<int>(CanvasT::kWidth) * scale; ++x) {
            const auto expected = canvas.get(visor::base::PointI{x / scale, y / scale}) ? lit : 0U;
            VISOR_CHECK_EQ(row[x], expected);
        }
    }
    XDestroyImage(shot);
}

/// Puts one word into the host's own queue. The honest seam on this Xorg:
/// it swallows XSendEvent even addressed back to the sender, so the
/// judgments hand the word over in-process instead.
void put_back_word(Display* host_display, const XEvent& word) {
    XEvent copy = word;
    // XLookupString walks the key event's own display field for the keymap;
    // a synthesized word carries a null one and crashes on arrival.
    copy.xany.display = host_display;
    XPutBackEvent(host_display, &copy);
}

} // namespace

void run_x11_host_tests() {
    using visor::base::PointI;
    using visor::canvas::Canvas;
    using visor::canvas::ColorRole;
    using Frame = Canvas<visor::base::SizeI{.w = kWidth, .h = kHeight}>;

    VirtualDisplay const screen(":99");
    if (!screen.up()) {
        VISOR_CHECK(false && "Xvfb on :99 did not come up for the x11 host judgments");
        return;
    }

    // The table itself: three wired entries on a window that opened.
    {
        auto host = visor::host::x11::X11WindowHost::open(":99", kWidth, kHeight);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        const visor::host::HostTable table = host->table();
        VISOR_CHECK(table.flush != nullptr);
        VISOR_CHECK(table.poll_event != nullptr);
        VISOR_CHECK(table.clock_ms != nullptr);
        VISOR_CHECK_EQ(table.info.screen.w, kWidth);
        VISOR_CHECK_EQ(table.info.screen.h, kHeight);
        VISOR_CHECK_EQ(table.info.format, visor::host::PixelFormat::Mono1);
        const std::uint64_t now = table.now_ms();
        ::usleep(2000);
        VISOR_CHECK(table.now_ms() >= now + 2); // the clock walks
    }

    // A full frame lands bit for bit: ink reads back as the visual's lit
    // word, the dark panel as zero. The puts leave at the next poll now, so
    // the readback asks the host for a round first.
    {
        Frame frame;
        frame.set_clear(false);
        const visor::draw::Pen on_pen{frame, true};
        on_pen.fill({.pos = PointI{0, 0}, .size = {.w = kWidth, .h = 32}});
        on_pen.fill({.pos = PointI{100, 40}, .size = {.w = 1, .h = 20}}); // an odd column
        auto host = visor::host::x11::X11WindowHost::open(":99", kWidth, kHeight);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        VISOR_CHECK(host->table().submit_frame(frame));
        visor::host::Event event;
        VISOR_CHECK(!host->table().poll(event)); // the drain rides the poll
        XSync(screen.display(), False);
        compare_readback(screen.display(), host->window(), frame);
        VISOR_CHECK_EQ(host->flush_count(), 1);
    }

    // A partial region at an odd x moves only itself: the region reads back
    // new, the complement keeps the old words, and the neighbor bits around
    // the region's byte edges survive the paint.
    {
        Frame frame;
        frame.set_clear(false);
        const visor::draw::Pen on_pen{frame, true};
        on_pen.fill({.pos = PointI{0, 0}, .size = {.w = kWidth, .h = kHeight}});
        auto host = visor::host::x11::X11WindowHost::open(":99", kWidth, kHeight);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        VISOR_CHECK(host->table().submit_frame(frame));
        visor::host::Event event;
        VISOR_CHECK(!host->table().poll(event));
        XSync(screen.display(), False);

        const visor::base::RectI region{.pos = PointI{5, 20}, .size = {.w = 33, .h = 24}};
        const visor::draw::Pen off_pen{frame, false};
        off_pen.fill(region); // punch a dark hole, byte-unaligned both edges
        // The contract: pixels points at the region's own first byte, not the
        // canvas base -- offset to the region's top row and leading byte.
        const std::uint8_t* region_pixels =
            frame.pixels().data() + (static_cast<std::size_t>(region.pos.y) * Frame::kStrideBytes) +
            (static_cast<std::size_t>(region.pos.x) / 8);
        VISOR_CHECK(host->table().submit_region(region, region_pixels, Frame::kStrideBytes));
        VISOR_CHECK(!host->table().poll(event));
        XSync(screen.display(), False);
        compare_readback(screen.display(), host->window(), frame); // the hole and the rest
        VISOR_CHECK_EQ(host->flush_count(), 2);
    }

    // A region whose leading byte offset is nonzero (x=16): the source walk
    // starts at the region's own first byte, and a second hop past it would
    // paint the neighbors' bits into the hole. The puts ride the next poll
    // now, so each readback asks the host for a round first.
    {
        Frame frame;
        frame.set_clear(false);
        const visor::draw::Pen on_pen{frame, true};
        on_pen.fill({.pos = PointI{0, 0}, .size = {.w = kWidth, .h = kHeight}});
        auto host = visor::host::x11::X11WindowHost::open(":99", kWidth, kHeight);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        visor::host::Event event;
        VISOR_CHECK(host->table().submit_frame(frame));
        VISOR_CHECK(!host->table().poll(event));
        XSync(screen.display(), False);

        const visor::base::RectI region{.pos = PointI{16, 8}, .size = {.w = 33, .h = 20}};
        const visor::draw::Pen off_pen{frame, false};
        off_pen.fill(region);
        const std::uint8_t* region_pixels =
            frame.pixels().data() + (static_cast<std::size_t>(region.pos.y) * Frame::kStrideBytes) +
            (static_cast<std::size_t>(region.pos.x) / 8);
        VISOR_CHECK(host->table().submit_region(region, region_pixels, Frame::kStrideBytes));
        VISOR_CHECK(!host->table().poll(event));
        XSync(screen.display(), False);
        compare_readback(screen.display(), host->window(), frame);
        VISOR_CHECK_EQ(host->flush_count(), 2);
    }

    // The batching itself: a strip rebuild's bands arrive as separate regions
    // and leave as one merged put. Three bands of a 64-row screen sit in the
    // plan as a single rectangle, the poll drains them, and every pixel is
    // on the server after one round -- the five-band full rebuild of the
    // desktop example is this same shape scaled up.
    {
        Frame frame;
        frame.set_clear(false);
        const visor::draw::Pen on_pen{frame, true};
        on_pen.fill({.pos = PointI{0, 0}, .size = {.w = kWidth, .h = 8}});
        on_pen.fill({.pos = PointI{0, 16}, .size = {.w = kWidth, .h = 8}});
        auto host = visor::host::x11::X11WindowHost::open(":99", kWidth, kHeight);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        for (int top = 0; top < kHeight; top += 8) {
            const visor::base::RectI band{.pos = PointI{0, top}, .size = {.w = kWidth, .h = 8}};
            const std::uint8_t* band_pixels =
                frame.pixels().data() + (static_cast<std::size_t>(top) * Frame::kStrideBytes);
            VISOR_CHECK(host->table().submit_region(band, band_pixels, Frame::kStrideBytes));
        }
        VISOR_CHECK_EQ(host->flush_count(), 8);
        VISOR_CHECK_EQ(host->pending_submissions(), 1); // eight bands, one box
        visor::host::Event event;
        VISOR_CHECK(!host->table().poll(event));
        VISOR_CHECK_EQ(host->pending_submissions(), 0);
        XSync(screen.display(), False);
        compare_readback(screen.display(), host->window(), frame);
    }
    }

    // A color window: an RGB565 frame is widened to the visual's channels on
    // the way out, so white reads back as every channel bit and pure red as the
    // red mask alone. The masks come from the shot, not from a guess about this
    // server's visual.
    {
        using ColorFrame =
            Canvas<visor::base::SizeI{.w = kWidth, .h = kHeight}, visor::canvas::Rgb565Traits>;
        ColorFrame frame;
        frame.fill(visor::canvas::Rgb565Traits::from_ink(visor::canvas::Ink::white()));
        auto host = visor::host::x11::X11WindowHost::open(":99", kWidth, kHeight, 1,
                                                          visor::host::PixelFormat::RGB565);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        VISOR_CHECK(host->table().submit_frame(frame));
        visor::host::Event event;
        VISOR_CHECK(!host->table().poll(event));
        XSync(screen.display(), False);

        const auto read_word = [&](int x, int y) -> std::uint32_t {
            XImage* shot =
                XGetImage(screen.display(), host->window(), x, y, 1, 1, AllPlanes, ZPixmap);
            if (shot == nullptr) {
                return 0;
            }
            const std::uint32_t word = XGetPixel(shot, 0, 0);
            XDestroyImage(shot);
            return word;
        };
        XImage* whole =
            XGetImage(screen.display(), host->window(), 0, 0, kWidth, kHeight, AllPlanes, ZPixmap);
        VISOR_CHECK(whole != nullptr);
        if (whole != nullptr) {
            const std::uint32_t every_channel =
                whole->red_mask | whole->green_mask | whole->blue_mask;
            VISOR_CHECK_EQ(read_word(0, 0), every_channel);
            VISOR_CHECK_EQ(read_word(kWidth - 1, kHeight - 1), every_channel);
            XDestroyImage(whole);
        }

        // Two rows of pure red land as the red mask, the other channels dark.
        frame.fill(visor::canvas::Rgb565Traits::from_ink(visor::canvas::Ink::from_rgb(0xff0000)));
        VISOR_CHECK(host->table().submit_frame(frame));
        VISOR_CHECK(!host->table().poll(event));
        XSync(screen.display(), False);
        XImage* red_shot =
            XGetImage(screen.display(), host->window(), 0, 0, 1, 1, AllPlanes, ZPixmap);
        VISOR_CHECK(red_shot != nullptr);
        if (red_shot != nullptr) {
            VISOR_CHECK_EQ(read_word(0, 0), red_shot->red_mask);
            VISOR_CHECK_EQ(read_word(kWidth - 1, kHeight - 1), red_shot->red_mask);
            XDestroyImage(red_shot);
        }

        // A mono frame to a color window is refused like any other wrong format.
        const Frame mono_frame;
        VISOR_CHECK(host->table().submit_frame(mono_frame));
        VISOR_CHECK_EQ(host->rejected_count(), 1);
    }

    // A region leaving the window is refused and counted, nothing crashes.
    {
        auto host = visor::host::x11::X11WindowHost::open(":99", kWidth, kHeight);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        const visor::base::RectI outside{.pos = PointI{120, 0}, .size = {.w = 16, .h = 10}};
        // The call itself succeeds -- an empty table is the only false -- the
        // refusal shows up in the host's own ledger.
        VISOR_CHECK(host->table().submit_region(outside, nullptr, 16));
        VISOR_CHECK_EQ(host->rejected_count(), 1);
        VISOR_CHECK_EQ(host->flush_count(), 0);
    }

    // The event half end to end: synthesized X words come back through the
    // table as translated core words.
    {
        auto host = visor::host::x11::X11WindowHost::open(":99", kWidth, kHeight);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        const visor::host::HostTable table = host->table();

        XEvent word{};
        word.type = ButtonPress;
        word.xbutton.window = host->window();
        word.xbutton.button = Button1;
        word.xbutton.x = 5;
        word.xbutton.y = 9;
        put_back_word(host->display(), word);
        visor::host::Event event;
        VISOR_CHECK(table.poll(event));
        if (const auto* down = std::get_if<visor::host::PointerDown>(&event)) {
            VISOR_CHECK_EQ(down->button, 1U);
            VISOR_CHECK_EQ(down->pos.x, 5);
            VISOR_CHECK_EQ(down->pos.y, 9);
        } else {
            VISOR_CHECK(false && "expected a PointerDown");
        }

        word.type = MotionNotify;
        word.xmotion.window = host->window();
        word.xmotion.x = 40;
        word.xmotion.y = -3;
        put_back_word(host->display(), word);
        VISOR_CHECK(table.poll(event));
        if (const auto* move = std::get_if<visor::host::PointerMove>(&event)) {
            VISOR_CHECK_EQ(move->pos.x, 40);
            VISOR_CHECK_EQ(move->pos.y, -3);
        } else {
            VISOR_CHECK(false && "expected a PointerMove");
        }

        // keycode 24 is 'q' on the default keymap: a real keysym round trip
        // through XLookupString and the B20 table.
        word.type = KeyPress;
        word.xkey.window = host->window();
        word.xkey.keycode = 24;
        put_back_word(host->display(), word);
        VISOR_CHECK(table.poll(event));
        if (const auto* down = std::get_if<visor::host::KeyDown>(&event)) {
            VISOR_CHECK_EQ(down->code, 0x71U); // 'q' as a code point
        } else {
            VISOR_CHECK(false && "expected a KeyDown");
        }
    }

    // The close word: the window manager's goodbye reads as close_requested,
    // not as a core event.
    {
        auto host = visor::host::x11::X11WindowHost::open(":99", kWidth, kHeight);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        const Atom protocols = XInternAtom(screen.display(), "WM_PROTOCOLS", False);
        const Atom delete_window = XInternAtom(screen.display(), "WM_DELETE_WINDOW", False);
        XEvent word{};
        word.type = ClientMessage;
        word.xclient.window = host->window();
        word.xclient.message_type = protocols;
        word.xclient.format = 32;
        word.xclient.data.l[0] = static_cast<long>(delete_window);
        put_back_word(host->display(), word);
        visor::host::Event event;
        VISOR_CHECK(!host->table().poll(event));
        VISOR_CHECK(host->close_requested());
    }

    // The expose replay: after the server loses the pixels, one Expose
    // restores them from the shadow without the core repainting a thing.
    {
        Frame frame;
        frame.set_clear(false);
        const visor::draw::Pen on_pen{frame, true};
        on_pen.fill({.pos = PointI{10, 10}, .size = {.w = 60, .h = 40}});
        auto host = visor::host::x11::X11WindowHost::open(":99", kWidth, kHeight);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        VISOR_CHECK(host->table().submit_frame(frame));
        visor::host::Event event;
        VISOR_CHECK(!host->table().poll(event)); // the frame takes the screen
        XSync(screen.display(), False);

        XClearWindow(screen.display(), host->window());
        XSync(screen.display(), False);
        Frame blank;
        blank.set_clear(false);
        compare_readback(screen.display(), host->window(), blank); // truly wiped

        XEvent word{};
        word.type = Expose;
        word.xexpose.window = host->window();
        word.xexpose.x = 0;
        word.xexpose.y = 0;
        word.xexpose.width = kWidth;
        word.xexpose.height = kHeight;
        word.xexpose.count = 0;
        put_back_word(host->display(), word);
        VISOR_CHECK(!host->table().poll(event)); // no core word
        XSync(screen.display(), False);
        compare_readback(screen.display(), host->window(), frame); // the shadow answered
    }

    // A resize hands the host exposes larger than the canvas: the WM's size
    // hints are advice, not law (the author's own smoke crashed exactly
    // here, stretching the window on WSLg). The host clips every expose to
    // the canvas it owns and repaints only that corner, heap intact.
    {
        Frame frame;
        frame.set_clear(false);
        const visor::draw::Pen on_pen{frame, true};
        on_pen.fill({.pos = PointI{0, 0}, .size = {.w = 64, .h = 64}});
        auto host = visor::host::x11::X11WindowHost::open(":99", kWidth, kHeight);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        VISOR_CHECK(host->table().submit_frame(frame));
        visor::host::Event event;
        VISOR_CHECK(!host->table().poll(event));
        XSync(screen.display(), False);
        XClearWindow(screen.display(), host->window());
        XSync(screen.display(), False);

        XEvent word{};
        word.type = Expose;
        word.xexpose.window = host->window();
        word.xexpose.x = -20; // starts left of the canvas
        word.xexpose.y = -20;
        word.xexpose.width = 640; // far past the canvas corner
        word.xexpose.height = 480;
        word.xexpose.count = 0;
        put_back_word(host->display(), word);
        VISOR_CHECK(!host->table().poll(event));
        XSync(screen.display(), False);
        compare_readback(screen.display(), host->window(), frame); // intact, no crash
    }

    // The zoom: scale 3 turns each canvas pixel into a 3x3 block on the
    // server, window and all, while the canvas and the shadow stay 1:1.
    // An OLED-sized canvas is stamp-sized on a desktop otherwise.
    {
        Frame frame;
        frame.set_clear(false);
        const visor::draw::Pen on_pen{frame, true};
        on_pen.fill({.pos = PointI{0, 0}, .size = {.w = kWidth, .h = 20}});
        on_pen.fill({.pos = PointI{7, 30}, .size = {.w = 3, .h = 10}}); // odd leading column
        auto host = visor::host::x11::X11WindowHost::open(":99", kWidth, kHeight, 3);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        VISOR_CHECK(host->table().submit_frame(frame));
        visor::host::Event event;
        VISOR_CHECK(!host->table().poll(event));
        XSync(screen.display(), False);
        compare_readback(screen.display(), host->window(), frame, 3); // 384x192 of blocks
        VISOR_CHECK_EQ(host->flush_count(), 1);
    }

    // A scale below 1 is an answer, not a crash: the same empty optional a
    // missing display gets.
    {
        const auto host = visor::host::x11::X11WindowHost::open(":99", kWidth, kHeight, 0);
        VISOR_CHECK(!host.has_value());
    }

    // Pointer words arrive in window coordinates, the core speaks canvas
    // coordinates: a scaled host must divide the position back before the
    // translation, or every click lands scale-fold too far out.
    {
        auto host = visor::host::x11::X11WindowHost::open(":99", kWidth, kHeight, 3);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        const visor::host::HostTable table = host->table();

        XEvent word{};
        word.type = ButtonPress;
        word.xbutton.window = host->window();
        word.xbutton.button = Button1;
        word.xbutton.x = 24; // window pixels: canvas (8, 9) at scale 3
        word.xbutton.y = 27;
        put_back_word(host->display(), word);
        visor::host::Event event;
        VISOR_CHECK(table.poll(event));
        if (const auto* down = std::get_if<visor::host::PointerDown>(&event)) {
            VISOR_CHECK_EQ(down->pos.x, 8);
            VISOR_CHECK_EQ(down->pos.y, 9);
        } else {
            VISOR_CHECK(false && "expected a PointerDown");
        }

        word.type = MotionNotify;
        word.xmotion.window = host->window();
        word.xmotion.x = 40; // 13 remainder 1: floor back to canvas
        word.xmotion.y = -3; // negative drifts stay negative
        put_back_word(host->display(), word);
        VISOR_CHECK(table.poll(event));
        if (const auto* move = std::get_if<visor::host::PointerMove>(&event)) {
            VISOR_CHECK_EQ(move->pos.x, 13);
            VISOR_CHECK_EQ(move->pos.y, -1);
        } else {
            VISOR_CHECK(false && "expected a PointerMove");
        }
    }
}
