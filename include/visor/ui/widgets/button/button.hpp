#pragma once

#include <cstdint>

#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

/**
 * @brief The momentary control: lights up while pressed, fires on release.
 *
 * The press handlers carry no position on purpose: hit testing already decided
 * which widget receives the event, so the button only flips its state. The
 * pixel difference between the two states is the button's whole feedback.
 *
 * Two sources can hold the press, pointer and keyboard, and either release
 * fires its own activation -- pressing Enter while holding the pointer asks
 * for two activations and gets them. The pressed look is the OR of both
 * sources: whichever still holds keeps the button dark.
 *
 * Keyboard activation completes on the key's up, not its down: a momentary
 * button's whole point is that the press is seen before the effect lands,
 * and the run loop's one-key-per-round rule guarantees exactly one frame
 * between the two. Checkbox is the deliberate contrast -- its flip persists
 * and paints by itself, so it acts on the down and ignores the up.
 *
 * Activation feedback is a plain function plus a context word: no closure,
 * no std::function, the vocabulary stays plain pointers so every consumer
 * link the whitelist allows can carry it.
 */
class Button : public Widget {
  public:
    /// @brief What a completed activation calls. context is the word handed
    ///        to set_on_activate, given back untouched.
    using ActivateHandler = void (*)(Button& self, void* context);

    using Widget::Widget;

    /// Buttons take the keyboard: focus traversal may land here.
    [[nodiscard]] bool focusable() const noexcept override { return true; }

    /// Wires the activation feedback. A button with no handler (or a null
    /// one) still tracks and paints its press; the release simply fires
    /// into the quiet, the same stance the empty host table takes.
    void set_on_activate(ActivateHandler handler, void* context = nullptr) noexcept;

    /// Whether either source currently holds the press.
    [[nodiscard]] bool pressed() const noexcept;

    void on_pointer_down() noexcept override;
    void on_pointer_up() noexcept override;
    void on_key_down(std::uint32_t code) noexcept override;
    void on_key_up(std::uint32_t code) noexcept override;

  protected:
    void on_draw(const Painter& painter) const noexcept override;

  private:
    /// Fires the activation if a handler is wired.
    void activate() noexcept;

    bool pointer_pressed_ = false;
    bool key_pressed_ = false;
    std::uint32_t held_key_ = 0; // valid while key_pressed_
    ActivateHandler on_activate_ = nullptr;
    void* activate_context_ = nullptr;
};

} // namespace visor::ui
