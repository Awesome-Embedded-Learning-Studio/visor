#include "visor/ui/input_router.hpp"

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

// The press names the owner (a press that finds nobody owns nothing), and the
// owner keeps the moves and collects the release wherever it lands.
void deliver(Widget& root, Widget*& press_owner, const host::PointerDown& word) noexcept {
    press_owner = root.hit_test(word.pos);
    if (press_owner != nullptr) {
        press_owner->on_pointer_down(word.pos);
    }
}

void deliver([[maybe_unused]] Widget& root, Widget*& press_owner,
             const host::PointerUp& word) noexcept {
    // Capture: the release belongs to the press's owner, not to whoever the
    // point happens to sit on. With no press held, a stray release reaches
    // nobody and changes nothing.
    if (press_owner != nullptr) {
        press_owner->on_pointer_up(word.pos);
        press_owner = nullptr;
    }
}

// A move is the owner's while a press is held (drag), hit testing's topmost
// widget otherwise (hover). The position rides along: the receiver under
// capture may be far outside its own bounds and must know where the pointer
// actually is.
void deliver(Widget& root, Widget*& press_owner, const host::PointerMove& word) noexcept {
    Widget* receiver = press_owner != nullptr ? press_owner : root.hit_test(word.pos);
    if (receiver != nullptr) {
        receiver->on_pointer_move(word.pos);
    }
}

// Tab is the tree's own semantic, so the dispatcher keeps it. Every other code
// goes to the focused widget verbatim: the control decides what it consumes
// against the codes events.hpp names, the core never decodes the rest.
void deliver(Widget& root, [[maybe_unused]] Widget*& press_owner,
             const host::KeyDown& word) noexcept {
    if (word.code == host::kKeyTab) {
        static_cast<void>(root.advance_focus());
        return;
    }
    if (Widget* holder = root.focused_widget(); holder != nullptr) {
        holder->on_key_down(word.code);
    }
}

// The release follows the same route as its press: to the focus holder,
// verbatim. A control that tracks nothing ignores it, a button that pressed
// on down completes its activation here.
void deliver(Widget& root, [[maybe_unused]] Widget*& press_owner,
             const host::KeyUp& word) noexcept {
    if (Widget* holder = root.focused_widget(); holder != nullptr) {
        holder->on_key_up(word.code);
    }
}

} // namespace

void dispatch_event(Widget& root, InputRouter& input, const host::Event& event) noexcept {
    std::visit([&root, &input](const auto& word) { deliver(root, input.press_owner_, word); },
               event);
}

} // namespace visor::ui
