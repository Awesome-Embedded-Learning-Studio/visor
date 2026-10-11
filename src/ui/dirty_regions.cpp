#include "visor/ui/dirty_regions.hpp"

#include "visor/base/geometry/rect.hpp"
#include "visor/base/helpers/rect_helper.hpp"

#include <cstddef>

namespace visor::ui {

void DirtyRegions::push(base::RectI area) noexcept {
    if (area.is_empty()) {
        return;
    }
    if (count_ == kCapacity) {
        // The full list folds into one box first, which frees the tail slots;
        // coverage of everything already here is the one promise that counts.
        collapse();
    }
    base::RectI grown = area;
    // Merge while the growing rectangle keeps touching residents. Every pass
    // retires one resident, so the walk terminates; the box only ever grows,
    // which can swallow empty space but never drops a covered pixel.
    bool merged = true;
    while (merged) {
        merged = false;
        for (std::size_t i = 0; i < count_; ++i) {
            if (!base::intersect(areas_[i], grown).is_empty()) {
                grown = base::bounding_box(areas_[i], grown);
                areas_[i] = areas_[count_ - 1];
                --count_;
                merged = true;
                break;
            }
        }
    }
    // At least one slot is free here: the list was not full on entry, or the
    // collapse above emptied kCapacity - 1 slots.
    areas_[count_] = grown;
    ++count_;
}

void DirtyRegions::collapse() noexcept {
    base::RectI box = areas_[0];
    for (std::size_t i = 1; i < count_; ++i) {
        box = base::bounding_box(box, areas_[i]);
    }
    areas_[0] = box;
    count_ = 1;
}

bool DirtyRegions::touches(base::RectI area) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (!base::intersect(areas_[i], area).is_empty()) {
            return true;
        }
    }
    return false;
}

} // namespace visor::ui
