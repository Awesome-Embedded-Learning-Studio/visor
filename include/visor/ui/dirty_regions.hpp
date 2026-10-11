#pragma once

#include "visor/base/geometry/rect.hpp"

#include <array>
#include <cstddef>

namespace visor::ui {

/**
 * @brief A fixed-capacity list of rectangles that owe a repaint.
 *
 * The invariant is coverage, never economy: a push onto a full list collapses
 * everything collected so far into one bounding box and carries on, so every
 * pixel ever pushed stays inside some reported region. Overflow degrades the
 * list's thrift, never its honesty. Overlapping pushes merge as they arrive,
 * which delays the collapse; the merge is thrift, the collapse is correctness.
 *
 * The renderer asks touches() per candidate strip and rebuilds the strips
 * told yes; a collapsed list answers yes for every strip its box spans, which
 * is more work than needed and exactly as much as owed.
 */
class DirtyRegions {
  public:
    /// Capacity before collapse. Sized for a scene's honest churn between two
    /// frames; the collapse is the safety net, not the working mode.
    static constexpr std::size_t kCapacity = 8;

    /// Records one rectangle for repaint. An empty rectangle records nothing:
    /// it owes no pixels. An overlap with a resident merges the two into their
    /// bounding box, cascading while the grown rectangle keeps touching others.
    void push(base::RectI area) noexcept;

    /// Empties the list; a submitted frame leaves nothing owed.
    void clear() noexcept { count_ = 0; }

    /// Regions currently on file, indices valid below count().
    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] base::RectI at(std::size_t index) const noexcept { return areas_[index]; }

    /// True when any region on file intersects area. Intersection, not
    /// containment: a strip clipping the corner of one dirty rectangle still
    /// owes its repaint.
    [[nodiscard]] bool touches(base::RectI area) const noexcept;

  private:
    /// Folds the whole list into one bounding box at slot zero. Coverage of
    /// everything pushed so far survives: the box contains every resident.
    void collapse() noexcept;

    std::array<base::RectI, kCapacity> areas_{};
    std::size_t count_ = 0;
};

} // namespace visor::ui
