#pragma once

#include <cstdint>

#include "visor/base/geometry/point.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

/**
 * @brief A rotary value control: a 270-degree face the pointer points at.
 *
 * The dial's gap sits at six o'clock: the minimum is lower-left, the maximum
 * lower-right, and the pointer crosses twelve o'clock between them. A press
 * anywhere in the square picks the angle under the pointer and turns the dial
 * to it; capture then makes the dial follow every move around the pivot until
 * the release. The four corners are grabbable on purpose -- the box is the
 * control's hit area -- and the exact center has no direction, so a press there
 * leaves the value alone. The 90-degree gap at the bottom clamps to the nearer
 * end rather than wrapping through.
 *
 * The pointer follows the hand: clockwise means more, counterclockwise less, in
 * every quadrant. This replaces the first version's relative vertical drag,
 * which read correctly at the sides but turned against the hand once the
 * pointer crossed twelve o'clock or four-thirty.
 *
 * Left and Right step the value by one under keyboard focus; Up and Down do
 * the same. Value feedback is a plain function plus a context word, the Slider
 * shape: no closure, no allocation.
 *
 * Drawing and the angle lookup are integer-only: the ring is two rounded
 * fills, the ticks and pointer ride a fixed-point sine approximation, and the
 * inverse direction is a 256-entry dot-product table, so no part of this
 * control needs floating point on any host.
 */
class Knob : public Widget {
  public:
    /// @brief What a value change calls. context is the word handed to
    ///        set_on_value, given back untouched.
    using ValueHandler = void (*)(Knob& self, void* context);

    /// @param minimum The value at the lower-left end of the face.
    /// @param maximum The value at the lower-right end; below minimum it
    ///        collapses onto minimum, the one-point dial.
    /// @param value The initial value, clamped into the range.
    Knob(base::RectI bounds, int minimum, int maximum, int value) noexcept;

    [[nodiscard]] int value() const noexcept;
    [[nodiscard]] int minimum() const noexcept;
    [[nodiscard]] int maximum() const noexcept;

    /// Sets the value, clamped into the range. A changed value reports the
    /// bounds as owed repaint and calls the handler; an unchanged one is
    /// silent, the quiet rewrite every state door here keeps.
    void set_value(int value) noexcept;

    /// Wires the value feedback. A knob with no handler still turns and
    /// paints; its changes fire into the quiet.
    void set_on_value(ValueHandler handler, void* context = nullptr) noexcept;

    [[nodiscard]] bool focusable() const noexcept override { return true; }

    void on_pointer_down(base::PointI at, host::Modifiers modifiers) noexcept override;
    void on_pointer_move(base::PointI at) noexcept override;
    void on_pointer_up([[maybe_unused]] base::PointI at) noexcept override;
    void on_key_down(std::uint32_t code, host::Modifiers modifiers) noexcept override;

  protected:
    void on_draw(const Painter& painter) const noexcept override;

  private:
    /// The disc's center; a non-square box centers the face and leaves the
    /// extra margin as ground.
    [[nodiscard]] base::PointI center() const noexcept;
    /// The disc's radius: half the shorter side.
    [[nodiscard]] int radius() const noexcept;
    /// Turns the dial to the angle of at around the pivot; the exact center
    /// has no direction and changes nothing.
    void update_value(base::PointI at) noexcept;

    bool dragging_ = false;
    int minimum_ = 0;
    int maximum_ = 0;
    int value_ = 0;
    ValueHandler on_value_ = nullptr;
    void* value_context_ = nullptr;
};

} // namespace visor::ui
