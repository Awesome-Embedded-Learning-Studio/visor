#include "visor/ui/input_router.hpp"

#include <cstdint>
#include <variant>

#include "visor/base/geometry/point.hpp"
#include "visor/bindings/events.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

namespace {

// One named deliverer per event word. std::visit instantiates the lambda body
// for every alternative, and each body resolves deliver() by overload
// resolution, so a future word without a deliver overload fails to compile
// instead of silently doing nothing. Adding a consumer means adding a function
// here, never editing the visit at the bottom.

/// The state one delivery reads and may write: press ownership, the pending
/// first click of a double click, the focus domain Tab obeys, and the round's
/// clock facts. Naming the group keeps every deliver overload one shape.
struct Delivery {
    Widget*& press_owner;
    std::uint32_t& last_click_id;
    base::PointI& last_click_pos;
    std::uint64_t& last_click_ms;
    std::uint32_t& press_button;
    host::Modifiers& press_modifiers;
    bool& has_last_click;
    Widget* focus_scope;
    std::uint64_t now_ms;
    bool clock_available;
};

// Two releases within this many pixels on each axis still belong to the same
// click pair. The distance is computed in 64 bits so extreme positions cannot
// overflow before the comparison.
bool within_click_slop(base::PointI first, base::PointI second) noexcept {
    const std::int64_t dx = static_cast<std::int64_t>(first.x) - second.x;
    const std::int64_t dy = static_cast<std::int64_t>(first.y) - second.y;
    constexpr std::int64_t k_slop = InputRouter::kDoubleClickSlopPx;
    return dx >= -k_slop && dx <= k_slop && dy >= -k_slop && dy <= k_slop;
}

// The press names the owner (a press that finds nobody owns nothing), and the
// owner keeps the moves and collects the release wherever it lands. A press on
// empty space breaks a pending double click; a press on a widget lets its
// release decide whether the pair matches.
void deliver(Widget& root, Delivery state, const host::PointerDown& word) noexcept {
    state.press_owner = root.hit_test(word.pos);
    state.press_button = word.button;
    state.press_modifiers = word.modifiers;
    if (state.press_owner == nullptr) {
        state.has_last_click = false;
    }
    if (state.press_owner != nullptr) {
        if (state.press_owner->focusable()) {
            if (Widget* previous = root.focused_widget();
                previous != nullptr && previous != state.press_owner) {
                previous->set_focused(false);
            }
            state.press_owner->set_focused(true);
        }
        state.press_owner->on_pointer_down(word.pos, word.modifiers);
    }
}

void deliver([[maybe_unused]] Widget const& root, Delivery state,
             const host::PointerUp& word) noexcept {
    // Capture: the release belongs to the press's owner, not to whoever the
    // point happens to sit on. With no press held, a stray release reaches
    // nobody and changes nothing.
    Widget* owner = state.press_owner;
    const std::uint32_t press_button = state.press_button;
    const host::Modifiers press_modifiers = state.press_modifiers;
    state.press_owner = nullptr;
    state.press_button = 0;
    state.press_modifiers = host::Modifiers{};
    // Judge the landing before the handler runs: a widget that resizes or
    // moves on release must not turn an outside release into a click.
    const bool inside = owner != nullptr && owner->bounds().contains(word.pos);
    if (owner != nullptr) {
        owner->on_pointer_up(word.pos);
    }

    // A click is a primary press and release that both belong to one widget
    // and finish inside its bounds. Everything else clears the pending first
    // click: a right click, a drag that ends elsewhere, a release with no
    // press, or a host with no clock.
    const bool primary = press_button == 1 && word.button == 1;
    if (!primary || !inside || !state.clock_available) {
        state.has_last_click = false;
        return;
    }

    // Unsigned subtraction is the wrap-safe way to ask "how long ago"; the
    // window includes its boundary.
    if (state.has_last_click && state.last_click_id == owner->id() &&
        state.now_ms - state.last_click_ms <= InputRouter::kDoubleClickWindowMs &&
        within_click_slop(state.last_click_pos, word.pos)) {
        // One pair, one double click. A third click starts a new pair.
        state.has_last_click = false;
        owner->on_double_click(word.pos, press_modifiers);
        return;
    }

    state.last_click_id = owner->id();
    state.last_click_pos = word.pos;
    state.last_click_ms = state.now_ms;
    state.has_last_click = true;
}

// A move is the owner's while a press is held (drag), hit testing's topmost
// widget otherwise (hover). The position rides along: the receiver under
// capture may be far outside its own bounds and must know where the pointer
// actually is.
void deliver(Widget& root, Delivery state, const host::PointerMove& word) noexcept {
    Widget* receiver = state.press_owner != nullptr ? state.press_owner : root.hit_test(word.pos);
    if (receiver != nullptr) {
        receiver->on_pointer_move(word.pos);
    }
}

void deliver(Widget& root, [[maybe_unused]] Delivery state,
             const host::PointerWheel& word) noexcept {
    // A wheel is a hover word, not part of the press session: it goes to the
    // topmost widget under the pointer and never changes who owns the press.
    if (Widget* receiver = root.hit_test(word.pos); receiver != nullptr) {
        receiver->on_pointer_wheel(word);
    }
}

// Tab is the tree's own semantic, so the dispatcher keeps it. Every other code
// goes to the focused widget verbatim: the control decides what it consumes
// against the codes events.hpp names, the core never decodes the rest.
void deliver(Widget& root, Delivery state, const host::KeyDown& word) noexcept {
    if (word.code == host::kKeyTab) {
        static_cast<void>(root.advance_focus(state.focus_scope));
        return;
    }
    if (Widget* holder = root.focused_widget(); holder != nullptr) {
        holder->on_key_down(word.code, word.modifiers);
    }
}

// The release follows the same route as its press: to the focus holder,
// verbatim. A control that tracks nothing ignores it, a button that pressed
// on down completes its activation here.
void deliver(Widget& root, [[maybe_unused]] Delivery state, const host::KeyUp& word) noexcept {
    if (Widget* holder = root.focused_widget(); holder != nullptr) {
        holder->on_key_up(word.code, word.modifiers);
    }
}

} // namespace

void dispatch_event(Widget& root, InputRouter& input, const host::Event& event,
                    std::uint64_t now_ms, bool clock_available) noexcept {
    std::visit(
        [&root, &input, now_ms, clock_available](const auto& word) {
            deliver(root,
                    Delivery{
                        .press_owner = input.press_owner_,
                        .last_click_id = input.last_click_id_,
                        .last_click_pos = input.last_click_pos_,
                        .last_click_ms = input.last_click_ms_,
                        .press_button = input.press_button_,
                        .press_modifiers = input.press_modifiers_,
                        .has_last_click = input.has_last_click_,
                        .focus_scope = input.focus_scope(),
                        .now_ms = now_ms,
                        .clock_available = clock_available,
                    },
                    word);
        },
        event);
}

} // namespace visor::ui
