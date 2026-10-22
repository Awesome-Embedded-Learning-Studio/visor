#include "visor/ui/popup_layer.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

PopupLayer::PopupLayer(base::RectI bounds) noexcept : Widget{bounds} {
    set_visible(false);
}

bool PopupLayer::open(Widget* content) noexcept {
    if (content == nullptr || content == this || content_ != nullptr) {
        return false;
    }
    if (!add_child(content)) {
        return false;
    }
    content_ = content;
    set_visible(true);
    static_cast<void>(raise());
    return true;
}

Widget* PopupLayer::close() noexcept {
    if (content_ == nullptr) {
        return nullptr;
    }
    Widget* content = content_;
    if (!remove_child(content)) {
        return nullptr; // the layer stays open if the child cannot detach
    }
    content_ = nullptr;
    set_visible(false);
    return content;
}

void PopupLayer::set_on_dismiss(DismissHandler handler, void* context) noexcept {
    on_dismiss_ = handler;
    dismiss_context_ = context;
}

void PopupLayer::on_pointer_down([[maybe_unused]] base::PointI at,
                                 [[maybe_unused]] host::Modifiers modifiers) noexcept {
    if (content_ == nullptr) {
        return;
    }
    Widget* content = close();
    if (on_dismiss_ != nullptr) {
        on_dismiss_(*this, *content, dismiss_context_);
    }
}

} // namespace visor::ui
