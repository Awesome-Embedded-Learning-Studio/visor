#include "visor/bindings/x11/x11_window_host.hpp"

#include "visor/base/bit_ops/bitmask.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/base/helpers/rect_helper.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"
#include "visor/bindings/x11/event_wire.hpp"
#include "visor/canvas/mono_canvas.hpp"

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace visor::host::x11 {
namespace {

// What the window listens for: repaints, the keyboard, and the pointer's
// three words. Resize is deliberately absent -- the window is pinned to the
// canvas size at birth.
constexpr long kEventMask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask |
                            ButtonReleaseMask | PointerMotionMask;

// The goodbye ignores X errors: a window the manager already forced away
// answers our destroy with a BadWindow, and a dying host must not turn that
// into the process's exit status. The signature is Xlib's own (XSetErrorHandler
// wants exactly these parameter types), so the pointees stay as declared there.
int ignore_x_errors([[maybe_unused]] Display* display,     // NOLINT(misc-const-correctness)
                    [[maybe_unused]] XErrorEvent* event) { // NOLINT(misc-const-correctness)
    return 0;
}

} // namespace

std::optional<X11WindowHost> X11WindowHost::open(const char* display_name, int width, int height,
                                                 int scale) noexcept {
    if (scale < 1) {
        return std::nullopt;
    }
    Display* display = XOpenDisplay(display_name);
    if (display == nullptr) {
        return std::nullopt;
    }
    const int screen = DefaultScreen(display);
    const unsigned long dark = BlackPixel(display, screen);
    const Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0,
                                              static_cast<unsigned>(width * scale),
                                              static_cast<unsigned>(height * scale), 0, dark, dark);
    XStoreName(display, window, "visor");
    XSizeHints hints{};
    hints.flags = PMinSize | PMaxSize;
    hints.min_width = width * scale;
    hints.max_width = width * scale;
    hints.min_height = height * scale;
    hints.max_height = height * scale;
    XSetNormalHints(display, window, &hints);
    XSelectInput(display, window, kEventMask);
    Atom const wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete, 1);
    GC gc = XCreateGC(display, window, 0, nullptr);
    XMapWindow(display, window);
    XFlush(display);

    X11WindowHost host;
    host.display_ = display;
    host.window_ = window;
    host.gc_ = gc;
    host.wm_protocols_atom_ = wm_protocols;
    host.wm_delete_atom_ = wm_delete;
    host.depth_ = DefaultDepth(display, screen);
    const Visual* visual = DefaultVisual(display, screen);
    host.red_mask_ = static_cast<std::uint32_t>(visual->red_mask);
    host.green_mask_ = static_cast<std::uint32_t>(visual->green_mask);
    host.blue_mask_ = static_cast<std::uint32_t>(visual->blue_mask);
    // The panel is dark, the ink is lit: every channel the visual offers.
    host.lit_word_ = host.red_mask_ | host.green_mask_ | host.blue_mask_;
    host.width_ = width;
    host.height_ = height;
    host.scale_ = scale;
    const std::size_t stride = (static_cast<std::size_t>(width) + 7) / 8;
    host.shadow_.assign(stride * static_cast<std::size_t>(height), 0);
    host.expanded_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                              static_cast<std::size_t>(scale) * static_cast<std::size_t>(scale),
                          0);
    return host;
}

X11WindowHost::~X11WindowHost() {
    release();
}

X11WindowHost::X11WindowHost(X11WindowHost&& other) noexcept {
    *this = std::move(other);
}

X11WindowHost& X11WindowHost::operator=(X11WindowHost&& other) noexcept {
    if (this != &other) {
        release();
        display_ = other.display_;
        window_ = other.window_;
        gc_ = other.gc_;
        wm_protocols_atom_ = other.wm_protocols_atom_;
        wm_delete_atom_ = other.wm_delete_atom_;
        depth_ = other.depth_;
        lit_word_ = other.lit_word_;
        red_mask_ = other.red_mask_;
        green_mask_ = other.green_mask_;
        blue_mask_ = other.blue_mask_;
        width_ = other.width_;
        height_ = other.height_;
        scale_ = other.scale_;
        close_requested_ = other.close_requested_;
        flush_count_ = other.flush_count_;
        rejected_count_ = other.rejected_count_;
        shadow_ = std::move(other.shadow_);
        expanded_ = std::move(other.expanded_);
        other.display_ = nullptr;
        other.window_ = None;
        other.gc_ = nullptr;
    }
    return *this;
}

void X11WindowHost::release() noexcept {
    if (display_ == nullptr) {
        return;
    }
    // An orderly goodbye of our own, not the echo of a dropped connection:
    // destroy the window, let the server settle, then close.
    XSetErrorHandler(&ignore_x_errors);
    XDestroyWindow(display_, window_);
    XSync(display_, False);
    XSetErrorHandler(nullptr);
    XFreeGC(display_, gc_);
    XCloseDisplay(display_);
    display_ = nullptr;
}

HostTable X11WindowHost::table() noexcept {
    HostTable table;
    table.flush = &X11WindowHost::flush_thunk;
    table.poll_event = &X11WindowHost::poll_event_thunk;
    table.clock_ms = &X11WindowHost::clock_thunk;
    table.context = this;
    return table;
}

bool X11WindowHost::close_requested() const noexcept {
    return close_requested_;
}

int X11WindowHost::flush_count() const noexcept {
    return flush_count_;
}

int X11WindowHost::rejected_count() const noexcept {
    return rejected_count_;
}

Window X11WindowHost::window() const noexcept {
    return window_;
}

Display* X11WindowHost::display() const noexcept {
    return display_;
}

void X11WindowHost::handle_flush(const FlushRegion& region) noexcept {
    const bool inside = region.format == PixelFormat::Mono1 && region.pixels != nullptr &&
                        !region.rect.is_empty() && region.rect.pos.x >= 0 &&
                        region.rect.pos.y >= 0 &&
                        region.rect.pos.x + region.rect.size.w <= width_ &&
                        region.rect.pos.y + region.rect.size.h <= height_;
    if (!inside) {
        ++rejected_count_;
        return;
    }
    paint_shadow(region);
    ++flush_count_;
    put_region(region.rect);
}

void X11WindowHost::paint_shadow(const FlushRegion& region) noexcept {
    // Pixel-by-pixel, MSB-first, the canvas's own packing. The per-byte fast
    // path lives in the offscreen host; a second shopper is when it moves out.
    const std::size_t dst_stride = (static_cast<std::size_t>(width_) + 7) / 8;
    const std::size_t first_src_byte = static_cast<std::size_t>(region.rect.pos.x) / 8;
    for (int row = 0; row < region.rect.size.h; ++row) {
        const std::uint8_t* src =
            region.pixels + (static_cast<std::size_t>(row) * region.stride_bytes) + first_src_byte;
        std::uint8_t* dst =
            shadow_.data() + (static_cast<std::size_t>(region.rect.pos.y + row) * dst_stride);
        for (int col = 0; col < region.rect.size.w; ++col) {
            const std::size_t x =
                static_cast<std::size_t>(region.rect.pos.x) + static_cast<std::size_t>(col);
            const unsigned mask =
                base::bit::mask_bit<std::uint8_t>(canvas::pixel_bit_position(x)).raw;
            std::uint8_t& byte = dst[x / 8];
            if ((src[(x / 8) - first_src_byte] & mask) != 0) {
                byte = static_cast<std::uint8_t>(byte | mask);
            } else {
                byte = static_cast<std::uint8_t>(byte & ~mask);
            }
        }
    }
}

void X11WindowHost::put_region(base::RectI area) noexcept {
    // The shadow's mono rows become 32bpp words, each canvas pixel a
    // scale-by-scale block, then one XPutImage carries them to the window.
    // The XImage describes our buffer; the server reads the masks and byte
    // order from it, nothing is converted on our side.
    const std::size_t canvas_stride = (static_cast<std::size_t>(width_) + 7) / 8;
    const std::size_t window_stride =
        static_cast<std::size_t>(width_) * static_cast<std::size_t>(scale_);
    const std::size_t out_x =
        static_cast<std::size_t>(area.pos.x) * static_cast<std::size_t>(scale_);
    for (int row = 0; row < area.size.h; ++row) {
        const std::uint8_t* src =
            shadow_.data() + (static_cast<std::size_t>(area.pos.y + row) * canvas_stride);
        for (int block_row = 0; block_row < scale_; ++block_row) {
            const std::size_t out_row =
                (static_cast<std::size_t>(area.pos.y + row) * static_cast<std::size_t>(scale_)) +
                static_cast<std::size_t>(block_row);
            std::uint32_t* out = expanded_.data() + (out_row * window_stride) + out_x;
            for (int col = 0; col < area.size.w; ++col) {
                const std::size_t x =
                    static_cast<std::size_t>(area.pos.x) + static_cast<std::size_t>(col);
                const unsigned mask =
                    base::bit::mask_bit<std::uint8_t>(canvas::pixel_bit_position(x)).raw;
                const std::uint32_t word = (src[x / 8] & mask) != 0 ? lit_word_ : 0;
                for (int stretch = 0; stretch < scale_; ++stretch) {
                    *out++ = word;
                }
            }
        }
    }
    XImage image{};
    image.width = area.size.w * scale_;
    image.height = area.size.h * scale_;
    image.xoffset = 0;
    image.format = ZPixmap;
    // The blocks were written where they sit in the window, so the image
    // reads from the region's own corner, not the buffer's first word.
    const std::size_t data_offset =
        (static_cast<std::size_t>(area.pos.y) * static_cast<std::size_t>(scale_) * window_stride) +
        out_x;
    image.data = reinterpret_cast<char*>(expanded_.data() + data_offset);
    image.byte_order = ImageByteOrder(display_);
    image.bitmap_unit = BitmapUnit(display_);
    image.bitmap_bit_order = BitmapBitOrder(display_);
    image.depth = depth_;
    image.bits_per_pixel = 32;
    image.bytes_per_line = static_cast<int>(window_stride) * 4;
    image.red_mask = red_mask_;
    image.green_mask = green_mask_;
    image.blue_mask = blue_mask_;
    XPutImage(display_, window_, gc_, &image, 0, 0, area.pos.x * scale_, area.pos.y * scale_,
              static_cast<unsigned>(area.size.w) * static_cast<unsigned>(scale_),
              static_cast<unsigned>(area.size.h) * static_cast<unsigned>(scale_));
    XFlush(display_);
}

bool X11WindowHost::poll_one(Event& out) noexcept {
    for (;;) {
        XEvent raw{};
        // The close word travels as a ClientMessage, which no event mask
        // selects; it gets its own typed check before the masked queue.
        if (XCheckTypedWindowEvent(display_, window_, ClientMessage, &raw) == True) {
            const unsigned long word = raw.xclient.data.l[0];
            if (raw.xclient.message_type == wm_protocols_atom_ && word == wm_delete_atom_) {
                close_requested_ = true;
            }
            return false;
        }
        if (XCheckWindowEvent(display_, window_, kEventMask, &raw) == False) {
            return false;
        }
        switch (raw.type) {
            case Expose: {
                // The screen lost pixels; the shadow still has them. A resize
                // hands out exposes past the canvas corner (the size hints
                // are advice, not law), so clip to the rectangle the canvas
                // owns before anything touches the buffers: only that corner
                // is ours to restore, and an unclipped put would write past
                // them.
                const base::RectI exposed{
                    .pos = base::PointI{raw.xexpose.x, raw.xexpose.y},
                    .size = base::SizeI{.w = raw.xexpose.width, .h = raw.xexpose.height},
                };
                const base::RectI owned =
                    base::intersect(exposed, base::RectI{
                                                 .pos = base::PointI{0, 0},
                                                 .size = base::SizeI{.w = width_, .h = height_},
                                             });
                if (!owned.is_empty()) {
                    put_region(owned);
                }
                continue;
            }
            case KeyPress:
            case KeyRelease: {
                KeySym keysym = NoSymbol;
                std::array<char, 8> buffer{};
                XLookupString(&raw.xkey, buffer.data(), static_cast<int>(buffer.size()), &keysym,
                              nullptr);
                const auto word =
                    translate_key(static_cast<std::uint32_t>(keysym), raw.type == KeyPress);
                if (word.has_value()) {
                    out = *word;
                    return true;
                }
                continue; // an unnamed key drops, the queue goes on
            }
            case ButtonPress:
            case ButtonRelease: {
                // Pointer words speak window pixels, the core speaks canvas
                // pixels: divide back before the translation, floor and all.
                raw.xbutton.x /= scale_;
                raw.xbutton.y /= scale_;
                const auto word = translate_button(raw.xbutton);
                if (word.has_value()) {
                    out = *word;
                    return true;
                }
                continue;
            }
            case MotionNotify:
                raw.xmotion.x /= scale_;
                raw.xmotion.y /= scale_;
                out = translate_motion(raw.xmotion);
                return true;
            default:
                continue; // the mask only selects the types handled above
        }
    }
}

void X11WindowHost::flush_thunk(void* context, const FlushRegion& region) noexcept {
    static_cast<X11WindowHost*>(context)->handle_flush(region);
}

bool X11WindowHost::poll_event_thunk(void* context, Event& out) noexcept {
    return static_cast<X11WindowHost*>(context)->poll_one(out);
}

std::uint64_t X11WindowHost::clock_thunk(void* /*context*/) noexcept {
    // Milliseconds on a steady clock since the first call: the epoch is the
    // host's own business, durations read by unsigned subtraction.
    static const std::chrono::steady_clock::time_point epoch = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - epoch).count());
}

} // namespace visor::host::x11
