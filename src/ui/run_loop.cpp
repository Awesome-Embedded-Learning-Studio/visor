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
            }
            // KeyDown/KeyUp wait for the focus chain, PointerMove for gestures;
            // both arrive in later batches on top of this same dispatcher.
        },
        event);
}

} // namespace visor::ui
