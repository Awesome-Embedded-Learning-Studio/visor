#include "visor/ui/run_loop.hpp"

#include <type_traits>
#include <variant>

#include "visor/bindings/events.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

void dispatch_event(Widget& root, const host::Event& event) noexcept {
    std::visit(
        [&root](const auto& word) {
            using Word = std::decay_t<decltype(word)>;
            if constexpr (std::is_same_v<Word, host::PointerDown>) {
                if (Widget* hit = root.hit_test(word.pos); hit != nullptr) {
                    hit->on_pointer_down();
                }
            } else if constexpr (std::is_same_v<Word, host::PointerUp>) {
                if (Widget* hit = root.hit_test(word.pos); hit != nullptr) {
                    hit->on_pointer_up();
                }
            } else if constexpr (std::is_same_v<Word, host::KeyDown>) {
                // The one named key code: focus traversal is the tree's own
                // semantic, so the dispatcher has to recognize it. Every other
                // code waits for its first key-consuming control.
                if (word.code == host::kKeyTab) {
                    static_cast<void>(root.advance_focus());
                }
            }
            // KeyUp and PointerMove wait for consumers: release tracking for
            // keyboard activation, gestures for the pointer. Both arrive on
            // top of this same dispatcher.
        },
        event);
}

} // namespace visor::ui
