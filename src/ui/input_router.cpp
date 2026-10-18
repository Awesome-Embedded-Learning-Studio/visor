#include "visor/ui/input_router.hpp"

#include <cstdint>
#include <variant>

#include "visor/bindings/events.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

namespace {

// One named deliverer per event word. std::visit instantiates the lambda body
// for every alternative, and each body resolves deliver() by overload
// resolution, so a future word without a deliver overload fails to compile
// instead of silently doing nothing. Adding a consumer means adding a function
// here, never editing the visit at the bottom.

/// The state one delivery reads: the press owner it may replace and the focus
/// domain Tab obeys. Naming the pair keeps every deliver overload one shape
/// instead of repeating two parameters in five signatures.
struct Delivery {
    Widget*& press_owner;
    Widget* focus_scope;
};

// The press names the owner (a press that finds nobody owns nothing), and the
// owner keeps the moves and collects the release wherever it lands.
void deliver(Widget& root, Delivery state, const host::PointerDown& word) noexcept {
    state.press_owner = root.hit_test(word.pos);
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
    if (state.press_owner != nullptr) {
        state.press_owner->on_pointer_up(word.pos);
        state.press_owner = nullptr;
    }
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
                    [[maybe_unused]] std::uint64_t now_ms) noexcept {
    std::visit(
        [&root, &input](const auto& word) {
            deliver(root,
                    Delivery{
                        .press_owner = input.press_owner_,
                        .focus_scope = input.focus_scope(),
                    },
                    word);
        },
        event);
}

} // namespace visor::ui
