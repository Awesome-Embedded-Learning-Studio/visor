#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"
#include "visor/bindings/wayland/wayland_window_host.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/pixel_convert.hpp"
#include "visor/draw/pen.hpp"

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include <fcntl.h>
#include <signal.h> // NOLINT(modernize-deprecated-headers): POSIX kill lives here, not in <csignal>
#include <sys/types.h>
#include <unistd.h>

#include <sys/wait.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace {

constexpr int kWidth = 128;
constexpr int kHeight = 64;

// The ink pair the window half expands into: lit is white, the panel dark.
constexpr std::uint32_t kLitWord = 0xffff'ffff;
constexpr std::uint32_t kDarkWord = 0xff00'0000;

/// Starts a private headless weston on a private socket unless one already
/// answers, and stops only what it started. A compositor that was already up
/// is somebody else's. The headless backend paints no screen, so its windows
/// are judged from the words the host itself wrote and the callbacks the
/// compositor itself sends -- Wayland offers no readback from the far side.
class HeadlessWeston {
  public:
    explicit HeadlessWeston(const char* socket) {
        if (::getenv("XDG_RUNTIME_DIR") == nullptr) {
            return; // weston cannot place its socket without one; the miss reports below
        }
        if (probe(socket)) {
            return;
        }
        const ::pid_t child = ::fork();
        if (child == 0) {
            // weston chats on its stderr; the judgment output stays ours.
            const int null_fd = ::open("/dev/null", O_WRONLY);
            if (null_fd >= 0) {
                ::dup2(null_fd, 1);
                ::dup2(null_fd, 2);
            }
            ::execlp("weston", "weston", "--backend=headless-backend.so", "--socket", socket,
                     "--idle-time=0", nullptr);
            ::_exit(127);
        }
        spawned_ = child;
        for (int waited = 0; waited < 30 && !up_; ++waited) {
            ::usleep(100000);
            if (probe(socket)) {
                break;
            }
        }
    }

    ~HeadlessWeston() {
        if (spawned_ > 0) {
            ::kill(spawned_, SIGTERM);
            int status = 0;
            ::waitpid(spawned_, &status, 0);
        }
    }

    HeadlessWeston(const HeadlessWeston&) = delete;
    HeadlessWeston& operator=(const HeadlessWeston&) = delete;

    /// Whether the socket answers.
    [[nodiscard]] bool up() const noexcept { return up_; }

  private:
    /// One connect attempt, closed at once: a probe, not a session.
    [[nodiscard]] bool probe(const char* socket) noexcept {
        wl_display* display = wl_display_connect(socket);
        if (display == nullptr) {
            return false;
        }
        wl_display_disconnect(display);
        up_ = true;
        return true;
    }

    bool up_ = false;
    ::pid_t spawned_ = -1;
};

/// The host's promise read back from its own window buffer: every canvas bit
/// lands as the lit word or the dark word, nothing in between, each canvas
/// pixel a scale-by-scale block. This is the client-side twin of the X11
/// readback judgment -- there the words come off the server with XGetImage,
/// here they come off the shared-memory frame the host filled itself.
template <typename CanvasT>
void compare_window(const visor::host::wayland::WaylandWindowHost& host, const CanvasT& canvas,
                    int scale = 1) {
    const std::uint32_t* words = host.submitted_words();
    VISOR_CHECK(words != nullptr);
    if (words == nullptr) {
        return;
    }
    const std::size_t stride = host.window_stride_words();
    for (int y = 0; y < static_cast<int>(CanvasT::kHeight) * scale; ++y) {
        const std::uint32_t* row = words + (static_cast<std::size_t>(y) * stride);
        for (int x = 0; x < static_cast<int>(CanvasT::kWidth) * scale; ++x) {
            const auto expected =
                canvas.get(visor::base::PointI{x / scale, y / scale}) ? kLitWord : kDarkWord;
            VISOR_CHECK_EQ(row[x], expected);
        }
    }
}

/// The compositor's own word that it consumed a frame: a frame callback
/// arrives only after the committed buffer has been drawn.
struct FrameWatch {
    bool done = false;
};

// The proxy's own listener table names this signature exactly, const or not.
void frame_done(void* data,
                [[maybe_unused]] wl_callback* callback, // NOLINT(misc-const-correctness)
                [[maybe_unused]] std::uint32_t callback_data) noexcept {
    static_cast<FrameWatch*>(data)->done = true;
}

const wl_callback_listener kFrameListener = {
    .done = frame_done,
};

} // namespace

void run_wayland_host_tests() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::canvas::Canvas;
    using visor::host::PixelFormat;
    using visor::host::wayland::WaylandWindowHost;
    using Frame = Canvas<visor::base::SizeI{.w = kWidth, .h = kHeight}>;

    HeadlessWeston const compositor("visor-headless-test");
    if (!compositor.up()) {
        VISOR_CHECK(false && "headless weston did not come up for the wayland host judgments");
        return;
    }

    // The table itself: three wired entries on a window that opened, and the
    // poll half answers silence with silence.
    {
        auto host = WaylandWindowHost::open("visor-headless-test", kWidth, kHeight);
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
        VISOR_CHECK_EQ(table.info.format, PixelFormat::Mono1);
        const std::uint64_t now = table.now_ms();
        ::usleep(2000);
        VISOR_CHECK(table.now_ms() >= now + 2); // the clock walks

        visor::host::Event ignored{};
        VISOR_CHECK(!table.poll(ignored)); // a still session has no words
    }

    // A full frame lands bit for bit: ink reads back as the lit word, the
    // dark panel as the dark word.
    {
        Frame frame;
        frame.set_clear(false);
        const visor::draw::Pen on_pen{frame, true};
        on_pen.fill({.pos = PointI{0, 0}, .size = {.w = kWidth, .h = 32}});
        on_pen.fill({.pos = PointI{100, 40}, .size = {.w = 1, .h = 20}}); // an odd column
        auto host = WaylandWindowHost::open("visor-headless-test", kWidth, kHeight);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        VISOR_CHECK(host->table().submit_frame(frame));
        compare_window(*host, frame);
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
        auto host = WaylandWindowHost::open("visor-headless-test", kWidth, kHeight);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        VISOR_CHECK(host->table().submit_frame(frame));

        const RectI region{.pos = PointI{5, 20}, .size = {.w = 33, .h = 24}};
        const visor::draw::Pen off_pen{frame, false};
        off_pen.fill(region); // punch a dark hole, byte-unaligned both edges
        // The contract: pixels points at the region's own first byte, not the
        // canvas base -- offset to the region's top row and leading byte.
        const std::uint8_t* region_pixels =
            frame.pixels().data() + (static_cast<std::size_t>(region.pos.y) * Frame::kStrideBytes) +
            (static_cast<std::size_t>(region.pos.x) / 8);
        VISOR_CHECK(host->table().submit_region(region, region_pixels, Frame::kStrideBytes));
        compare_window(*host, frame); // the hole and the rest
        VISOR_CHECK_EQ(host->flush_count(), 2);
    }

    // A region whose leading byte offset is nonzero (x=16): the caller has
    // already pointed pixels at the region's own first byte, so the source
    // walk starts there -- a second hop past it would read two bytes deep and
    // leave the region showing its neighbors' bits.
    {
        Frame frame;
        frame.set_clear(false);
        const visor::draw::Pen on_pen{frame, true};
        on_pen.fill({.pos = PointI{0, 0}, .size = {.w = kWidth, .h = kHeight}});
        auto host = WaylandWindowHost::open("visor-headless-test", kWidth, kHeight);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        VISOR_CHECK(host->table().submit_frame(frame));

        const RectI region{.pos = PointI{16, 8}, .size = {.w = 33, .h = 20}};
        const visor::draw::Pen off_pen{frame, false};
        off_pen.fill(region); // a dark hole whose first byte sits two bytes in
        const std::uint8_t* region_pixels =
            frame.pixels().data() + (static_cast<std::size_t>(region.pos.y) * Frame::kStrideBytes) +
            (static_cast<std::size_t>(region.pos.x) / 8);
        VISOR_CHECK(host->table().submit_region(region, region_pixels, Frame::kStrideBytes));
        compare_window(*host, frame);
        VISOR_CHECK_EQ(host->flush_count(), 2);
    }

    // The scale block: each canvas pixel lands as a scale-by-scale square.
    {
        Frame frame;
        frame.set_clear(false);
        const visor::draw::Pen on_pen{frame, true};
        on_pen.fill({.pos = PointI{10, 10}, .size = {.w = 3, .h = 2}});
        auto host = WaylandWindowHost::open("visor-headless-test", kWidth, kHeight, 3);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        VISOR_CHECK(host->table().submit_frame(frame));
        compare_window(*host, frame, 3);
    }

    // A host with history keeps its news across a move: the shadow epoch
    // travels with the object, the listeners re-home onto the new address,
    // and a partial region after the move paints without resurrecting the
    // first frame's dark pixels around itself.
    {
        Frame frame;
        frame.set_clear(false);
        const visor::draw::Pen on_pen{frame, true};
        on_pen.fill({.pos = PointI{0, 0}, .size = {.w = kWidth, .h = kHeight}});
        auto host = WaylandWindowHost::open("visor-headless-test", kWidth, kHeight);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        VISOR_CHECK(host->table().submit_frame(frame));

        auto moved = std::move(host); // the whole object, listeners and all
        const RectI region{.pos = PointI{10, 10}, .size = {.w = 8, .h = 8}};
        const visor::draw::Pen off_pen{frame, false};
        off_pen.fill(region);
        const std::uint8_t* region_pixels =
            frame.pixels().data() + (static_cast<std::size_t>(region.pos.y) * Frame::kStrideBytes) +
            (static_cast<std::size_t>(region.pos.x) / 8);
        VISOR_CHECK(moved->table().submit_region(region, region_pixels, Frame::kStrideBytes));
        compare_window(*moved, frame); // the hole new, the lit history intact
        VISOR_CHECK_EQ(moved->flush_count(), 2);
    }

    // A color host takes RGB565 and expands on the way out: the two probes
    // are pure red and pure blue, the words the converter's own line names.
    {
        auto host =
            WaylandWindowHost::open("visor-headless-test", kWidth, kHeight, 1, PixelFormat::RGB565);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        const std::array<std::uint8_t, 4> row = {0x00, 0xF8, 0x1F, 0x00}; // LE words 0xF800, 0x001F
        const RectI two{.pos = PointI{0, 0}, .size = {.w = 2, .h = 1}};
        VISOR_CHECK(host->table().submit_region(two, row.data(), 4, PixelFormat::RGB565));
        const std::uint32_t* words = host->submitted_words();
        VISOR_CHECK(words != nullptr);
        if (words != nullptr) {
            VISOR_CHECK_EQ(words[0], visor::canvas::rgb565_to_xrgb8888(0xF800));
            VISOR_CHECK_EQ(words[1], visor::canvas::rgb565_to_xrgb8888(0x001F));
            VISOR_CHECK_EQ(words[2], kDarkWord); // past the region keeps the panel
        }
    }

    // An XRGB8888 host passes words through untouched, X byte and all.
    {
        auto host = WaylandWindowHost::open("visor-headless-test", kWidth, kHeight, 1,
                                            PixelFormat::XRGB8888);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        const std::array<std::uint8_t, 4> row = {0xFF, 0x00, 0x00, 0xAA}; // LE word 0xAA0000FF
        const RectI one{.pos = PointI{0, 0}, .size = {.w = 1, .h = 1}};
        VISOR_CHECK(host->table().submit_region(one, row.data(), 4, PixelFormat::XRGB8888));
        const std::uint32_t* words = host->submitted_words();
        VISOR_CHECK(words != nullptr);
        if (words != nullptr) {
            VISOR_CHECK_EQ(words[0], 0xAA0000FFU);
        }
    }

    // The refusal line: wrong format, null pixels, an empty region, and a
    // rectangle leaving the window all count as rejections, never flushes.
    {
        auto host = WaylandWindowHost::open("visor-headless-test", kWidth, kHeight);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        const visor::host::HostTable table = host->table();
        const std::array<std::uint8_t, 4> row = {0, 0, 0, 0};

        // The return word says only whether a flush is wired (it stays true
        // while the host counts the refusal); the judgment reads the count
        // below, the same line the X11 judgments draw.
        VISOR_CHECK(table.submit_region(RectI{.pos = PointI{0, 0}, .size = {.w = 2, .h = 1}},
                                        row.data(), 4,
                                        PixelFormat::XRGB8888)); // the host speaks Mono1
        VISOR_CHECK(table.submit_region(RectI{.pos = PointI{0, 0}, .size = {.w = 2, .h = 1}},
                                        nullptr, 4, PixelFormat::Mono1));
        VISOR_CHECK(table.submit_region(RectI{.pos = PointI{0, 0}, .size = {.w = 0, .h = 0}},
                                        row.data(), 4, PixelFormat::Mono1));
        VISOR_CHECK(
            table.submit_region(RectI{.pos = PointI{0, 0}, .size = {.w = kWidth + 1, .h = 1}},
                                row.data(), 4, PixelFormat::Mono1));
        VISOR_CHECK_EQ(host->rejected_count(), 4);
        VISOR_CHECK_EQ(host->flush_count(), 0); // the first frame open() paints is no flush

        // A scale below 1 and a missing display are answers, not emergencies.
        VISOR_CHECK(
            !WaylandWindowHost::open("visor-headless-test", kWidth, kHeight, 0).has_value());
        VISOR_CHECK(
            !WaylandWindowHost::open("visor-headless-test-absent", kWidth, kHeight).has_value());
    }

    // A frame callback is the compositor's throttle hint for the next
    // repaint, not proof that this buffer reached the glass; waiting it out
    // proves the submission round trip -- request, commit, answer -- while
    // the readbacks above prove the pixels themselves.
    {
        auto host = WaylandWindowHost::open("visor-headless-test", kWidth, kHeight);
        VISOR_CHECK(host.has_value());
        if (!host.has_value()) {
            return;
        }
        Frame frame;
        frame.set_clear(false);
        const visor::draw::Pen on_pen{frame, true};
        on_pen.fill({.pos = PointI{0, 0}, .size = {.w = 16, .h = 16}});
        VISOR_CHECK(host->table().submit_frame(frame));

        FrameWatch watch;
        // The proxy hands itself to add_listener and destroy, both of which
        // take it non-const; the pointee-const suggestion has no place here.
        wl_callback* callback = wl_surface_frame(host->surface()); // NOLINT(misc-const-correctness)
        wl_callback_add_listener(callback, &kFrameListener, &watch);
        wl_surface_commit(host->surface()); // the callback rides the next commit
        for (int waited = 0; waited < 200 && !watch.done; ++waited) {
            wl_display_roundtrip(host->display()); // drains whatever already arrived
            ::usleep(10000);
        }
        wl_callback_destroy(callback);
        VISOR_CHECK(watch.done);
    }
}
