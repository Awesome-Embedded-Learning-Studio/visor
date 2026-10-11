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

// The one named key code: focus traversal is the tree's own semantic, so the
// dispatcher has to recognize it. Every other code waits for its first
// key-consuming control.
void deliver(Widget& root, const host::KeyDown& word) noexcept {
    if (word.code == host::kKeyTab) {
        static_cast<void>(root.advance_focus());
    }
}

// KeyUp and PointerMove have no consumer yet. The empty bodies are that
// policy in writing, not a gap. Release tracking lands on KeyUp for keyboard
// activation, PointerMove becomes gestures. Both arrive on this same set.
void deliver([[maybe_unused]] Widget& root, [[maybe_unused]] const host::KeyUp& word) noexcept {}

void deliver([[maybe_unused]] Widget& root,
             [[maybe_unused]] const host::PointerMove& word) noexcept {}

} // namespace

void dispatch_event(Widget& root, const host::Event& event) noexcept {
    std::visit([&root](const auto& word) { deliver(root, word); }, event);
}

} // namespace visor::ui
