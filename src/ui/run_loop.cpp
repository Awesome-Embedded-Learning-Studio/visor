#include "visor/ui/run_loop.hpp"

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
void deliver(Widget& root, const host::PointerDown& word) noexcept {
    if (Widget* hit = root.hit_test(word.pos); hit != nullptr) {
        hit->on_pointer_down();
    }
}

void deliver(Widget& root, const host::PointerUp& word) noexcept {
    if (Widget* hit = root.hit_test(word.pos); hit != nullptr) {
        hit->on_pointer_up();
    }
}

// Tab is the tree's own semantic, so the dispatcher keeps it. Every other code
// goes to the focused widget verbatim: the control decides what it consumes
// against the codes events.hpp names, the core never decodes the rest.
void deliver(Widget& root, const host::KeyDown& word) noexcept {
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
void deliver(Widget& root, const host::KeyUp& word) noexcept {
    if (Widget* holder = root.focused_widget(); holder != nullptr) {
        holder->on_key_up(word.code);
    }
}

// PointerMove has no consumer yet. The empty body is that policy in
// writing, not a gap: it becomes the gesture layer.
void deliver([[maybe_unused]] Widget& root,
             [[maybe_unused]] const host::PointerMove& word) noexcept {}

} // namespace

void dispatch_event(Widget& root, const host::Event& event) noexcept {
    std::visit([&root](const auto& word) { deliver(root, word); }, event);
}

} // namespace visor::ui
