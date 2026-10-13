#pragma once

#include "visor/bindings/events.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

/**
 * @brief The pointer's press ownership, held between events.
 *
 * A press owns the pointer: once a PointerDown lands on a widget (or on
 * nobody), every following PointerMove and the PointerUp belong to that same
 * destination regardless of where they fall, until the release ends the
 * ownership. That is capture, and it is what makes a stranded press
 * impossible (a release outside the pressed control still reaches it) and a
 * drag possible (the owner keeps hearing moves after the pointer leaves its
 * bounds).
 *
 * The router is caller-owned state beside the tree, the same stance
 * DirtyRegions takes: press ownership outlives a round, so no round-local
 * home would do. The single-pointer assumption is the spec's world (one
 * event queue, one consumer); one press session exists at a time, whatever
 * button id carried the down.
 */
class InputRouter {
  public:
    /// The widget the current press landed on, null while no press is held
    /// (including a press that hit nobody).
    [[nodiscard]] Widget* press_owner() const noexcept { return press_owner_; }

  private:
    // dispatch_event is the one writer: ownership changes only as events
    // arrive, never by hand.
    friend void dispatch_event(Widget& root, InputRouter& input, const host::Event& event) noexcept;

    Widget* press_owner_ = nullptr;
};

/**
 * @brief Delivers one event to the tree through the router. Declared here,
 *        defined in the source: the dispatcher knows every event word, the
 *        widgets know none of them.
 *
 * Pointer presses set the router's ownership (hit testing picks the first
 * owner; a press that finds nobody owns nothing); moves go to the owner
 * while a press is held and to hit testing's topmost widget otherwise (the
 * hover foundation); the release goes to the owner wherever it lands and
 * retires the ownership. Tab (the one named key code) still moves the focus
 * along the tree; every other key code goes to the focus holder verbatim.
 * The point or key that finds no receiver ends the round unconsumed beyond
 * the count.
 */
void dispatch_event(Widget& root, InputRouter& input, const host::Event& event) noexcept;

} // namespace visor::ui
