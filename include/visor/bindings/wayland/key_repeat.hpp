#pragma once

#include <cstdint>
#include <optional>

namespace visor::host::wayland {

/// Client-side repetition for the raw evdev keyboard. All deadlines use the
/// caller's monotonic microseconds, so tests need neither a seat nor sleeps.
class KeyRepeat {
  public:
    /// Older keyboards have no repeat_info event; use conventional defaults
    /// until the compositor supplies its own rate and delay.
    void configure(std::int32_t rate, std::int32_t delay_ms, std::uint64_t now_us) noexcept {
        if (rate <= 0 || delay_ms < 0) {
            interval_us_ = 0;
            cancel();
            return;
        }
        const auto frequency = static_cast<std::uint64_t>(rate);
        interval_us_ = (1'000'000 + frequency - 1) / frequency;
        delay_us_ = static_cast<std::uint64_t>(delay_ms) * 1000;
        if (key_.has_value()) {
            next_us_ = now_us + delay_us_;
        }
    }

    /// A modifier press leaves the current repeat running. A newly pressed
    /// repeatable key takes over; releasing it does not resume an older key.
    void key(std::uint32_t code, bool pressed, bool repeatable, std::uint64_t now_us) noexcept {
        if (!pressed) {
            if (key_ == code) {
                cancel();
            }
        } else if (repeatable && interval_us_ != 0) {
            key_ = code;
            next_us_ = now_us + delay_us_;
        }
    }

    void cancel() noexcept { key_.reset(); }

    /// Missed intervals collapse into one press, rather than a burst of old
    /// input after a slow frame. The next deadline stays on the same cadence.
    [[nodiscard]] std::optional<std::uint32_t> poll(std::uint64_t now_us) noexcept {
        if (!key_.has_value() || now_us < next_us_) {
            return std::nullopt;
        }
        next_us_ += (((now_us - next_us_) / interval_us_) + 1) * interval_us_;
        return key_;
    }

  private:
    std::uint64_t interval_us_ = 40'000;
    std::uint64_t delay_us_ = 600'000;
    std::uint64_t next_us_ = 0;
    std::optional<std::uint32_t> key_;
};

} // namespace visor::host::wayland
