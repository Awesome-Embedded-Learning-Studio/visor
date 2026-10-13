#pragma once

#include <cstdint>

#include "visor/base/geometry/point.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

/**
 * @brief A horizontal value control: drag the handle along the track.
 *
 * The first control built on the gesture foundation: the press grabs (the
 * value jumps to the press position, capture then routes every move to the
 * slider however far it wanders), the moves drag, the release lets go. The
 * value is the handle's left edge offset, 0 through max(), max being the
 * track width minus the handle -- the handle never leaves the track.
 *
 * No keyboard leg yet, on purpose: left and right arrows are host-defined
 * codes the core never names, and the slider will not guess at them. When a
 * batch names the arrow codes (or a host maps them onto named ones), the leg
 * arrives on the same value vocabulary.
 *
 * Value feedback is a plain function plus a context word, the Button shape:
 * no closure, no std::function, the vocabulary stays plain pointers.
 */
class Slider : public Widget {
  public:
    /// @brief What a value change calls. context is the word handed to
    ///        set_on_value, given back untouched.
    using ValueHandler = void (*)(Slider& self, void* context);

    explicit Slider(base::RectI bounds) noexcept;

    /// The current value: the handle's left edge, 0 through max().
    [[nodiscard]] int value() const noexcept;
    /// The largest value: the track width minus the handle. The handle is
    /// kHandleWidth wide, so at max() it still ends inside the track.
    [[nodiscard]] int max() const noexcept;

    /// Sets the value, clamped to the track. A changed value reports the
    /// bounds as owed repaint and calls the handler; an unchanged one
    /// reports and calls nothing, matching the no-op stances elsewhere.
    void set_value(int value) noexcept;

    /// Wires the value feedback. A slider with no handler (or a null one)
    /// still tracks and paints its value; changes fire into the quiet.
    void set_on_value(ValueHandler handler, void* context = nullptr) noexcept;

    void on_pointer_down(base::PointI at) noexcept override;
    void on_pointer_move(base::PointI at) noexcept override;
    void on_pointer_up([[maybe_unused]] base::PointI at) noexcept override;

  protected:
    void on_draw(const Painter& painter) const noexcept override;

  private:
    /// The handle's width in pixels. Two, so one pixel of track shows beside
    /// it at both ends of the range.
    static constexpr int kHandleWidth = 2;

    /// Maps a pointer x to the value it names, clamped to the track.
    [[nodiscard]] int value_at(int x) const noexcept;

    bool dragging_ = false;
    int value_ = 0;
    ValueHandler on_value_ = nullptr;
    void* value_context_ = nullptr;
};

} // namespace visor::ui
