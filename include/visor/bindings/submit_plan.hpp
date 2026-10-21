#pragma once

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/helpers/rect_helper.hpp"

#include <array>
#include <cstddef>

namespace visor::host {

/**
 * @brief The submission policy between the regions a core submits and the
 *        puts a host sends: clip to the window, merge what touches, collapse
 *        when full.
 *
 * A round of rendering hands the host its regions one call at a time -- a
 * strip rebuild arrives as five bands -- and each put is a request the wire
 * wants batched. The plan is the fixed-capacity stop between the two: every
 * incoming rectangle is clipped to the bounds the host owns, rectangles that
 * overlap or share an edge merge into their bounding box, and a push onto a
 * full list folds the residents into one box before the newcomer joins, the
 * dirty collector's own stance. Coverage is the one promise: every pixel
 * pushed is inside some planned rectangle when the batch goes out. Merging
 * may repaint pixels nobody pushed, which costs bytes and never correctness,
 * because the host repaints from a shadow that already holds the round's
 * pixels -- the same over-paint-is-fine trade the collector made.
 *
 * The one deliberate difference from the collector is the merge predicate.
 * The collector merges only where pixels truly overlap, because its word is
 * debt and a touched pixel is a fact. This plan also merges where rectangles
 * share an edge, because its word is batching and five stacked bands are one
 * put. Corner-touching rectangles stay separate: no shared edge, no merge.
 *
 * The plan holds no policy about when the batch executes. The X11 host
 * drains it when the next poll opens a round, so one round's regions become
 * one flight of puts followed by a single flush.
 */
class SubmitPlan {
  public:
    static constexpr std::size_t kCapacity = 8;

    /// The pre-open plan: empty bounds clip everything away, so a host built
    /// but not yet opened drops instead of batching. open() installs the real
    /// rectangle.
    constexpr SubmitPlan() noexcept = default;

    /// The bounds every push is clipped to, the host's own rectangle.
    explicit constexpr SubmitPlan(base::RectI bounds) noexcept : bounds_{bounds} {}

    /// Clips the area to the bounds and merges it with every resident it
    /// touches, cascading until no neighbor is left. An area missing the
    /// bounds entirely is dropped; a push onto a full list collapses the
    /// residents into one box first.
    constexpr void push(base::RectI area) noexcept {
        const base::RectI visible = base::intersect(area, bounds_);
        if (visible.is_empty()) {
            return;
        }
        if (count_ == kCapacity) {
            collapse();
        }
        base::RectI grown = visible;
        bool merged = true;
        while (merged) {
            merged = false;
            for (std::size_t i = 0; i < count_; ++i) {
                if (merges(areas_[i], grown)) {
                    grown = base::bounding_box(areas_[i], grown);
                    areas_[i] = areas_[count_ - 1];
                    --count_;
                    merged = true;
                    break;
                }
            }
        }
        areas_[count_] = grown;
        ++count_;
    }

    /// Planned rectangles, in no promised order. Coverage of everything
    /// pushed is the contract, arrangement is not.
    [[nodiscard]] constexpr std::size_t count() const noexcept { return count_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] constexpr base::RectI at(std::size_t index) const noexcept {
        return areas_[index];
    }

    /// Retires the batch without executing it; the drained host calls this
    /// after the puts go out.
    constexpr void clear() noexcept { count_ = 0; }

  private:
    /// True when the two rectangles overlap or share an edge of positive
    /// length. Half-open ranges: a strict overlap on one axis and a
    /// touch-or-overlap on the other. Corner contact alone merges nothing.
    [[nodiscard]] static constexpr bool merges(base::RectI a, base::RectI b) noexcept {
        const bool x_overlap = a.pos.x < (b.pos.x + b.size.w) && b.pos.x < (a.pos.x + a.size.w);
        const bool y_overlap = a.pos.y < (b.pos.y + b.size.h) && b.pos.y < (a.pos.y + a.size.h);
        const bool x_touch = a.pos.x <= (b.pos.x + b.size.w) && b.pos.x <= (a.pos.x + a.size.w);
        const bool y_touch = a.pos.y <= (b.pos.y + b.size.h) && b.pos.y <= (a.pos.y + a.size.h);
        return (x_overlap && y_touch) || (x_touch && y_overlap);
    }

    /// Folds every resident into one bounding box occupying the first slot,
    /// the overflow stance the dirty collector set.
    constexpr void collapse() noexcept {
        base::RectI box = areas_[0];
        for (std::size_t i = 1; i < count_; ++i) {
            box = base::bounding_box(areas_[i], box);
        }
        areas_[0] = box;
        count_ = 1;
    }

    base::RectI bounds_{};
    std::size_t count_ = 0;
    std::array<base::RectI, kCapacity> areas_{};
};

} // namespace visor::host
