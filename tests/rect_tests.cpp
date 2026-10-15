#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/base/helpers/point_helper.hpp"
#include "visor/base/helpers/rect_helper.hpp"

void run_rect_tests() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;

    // Emptiness: a default rect and any w/h <= 0 mean empty, no sign flipping.
    const RectI unit{.pos = PointI{0, 0}, .size = SizeI{.w = 1, .h = 1}};
    VISOR_CHECK(RectI{}.is_empty());
    VISOR_CHECK((RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 0, .h = 4}}.is_empty()));
    VISOR_CHECK((RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = -1}}.is_empty()));
    VISOR_CHECK(!unit.is_empty());

    // Half-open containment: [x, x+w) x [y, y+h). The right and bottom edges are outside.
    // r covers x in [10, 15) and y in [20, 25).
    const RectI r{.pos = PointI{10, 20}, .size = SizeI{.w = 5, .h = 5}};
    VISOR_CHECK(r.contains((PointI{10, 20})));    // top-left corner is inside
    VISOR_CHECK(r.contains((PointI{14, 24})));    // last covered pixel
    VISOR_CHECK(!r.contains((PointI{15, 20})));   // right edge is outside
    VISOR_CHECK(!r.contains((PointI{10, 25})));   // bottom edge is outside
    VISOR_CHECK(!r.contains((PointI{9, 20})));    // left of the area
    VISOR_CHECK(!r.contains((PointI{10, 19})));   // above the area
    VISOR_CHECK(!r.contains((PointI{100, 100}))); // far away

    // Negative coordinates follow the same half-open rules.
    const RectI neg{.pos = PointI{-5, -5}, .size = SizeI{.w = 10, .h = 10}};
    VISOR_CHECK(neg.contains((PointI{-5, -5})));
    VISOR_CHECK(neg.contains((PointI{4, 4})));
    VISOR_CHECK(!neg.contains((PointI{5, 5})));

    // An empty rect contains nothing, not even its own origin.
    const RectI empty{.pos = PointI{3, 3}, .size = SizeI{.w = 0, .h = 0}};
    VISOR_CHECK(!empty.contains((PointI{3, 3})));

    // Intersection: partial overlap returns the exact covered band.
    const RectI a{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 10}};
    const RectI b{.pos = PointI{5, 5}, .size = SizeI{.w = 10, .h = 10}};
    VISOR_CHECK_EQ(intersect(a, b), (RectI{.pos = PointI{5, 5}, .size = SizeI{.w = 5, .h = 5}}));

    // Containment and identity keep the smaller / the same rect.
    const RectI inner{.pos = PointI{2, 3}, .size = SizeI{.w = 4, .h = 4}};
    VISOR_CHECK_EQ(intersect(a, inner), inner);
    VISOR_CHECK_EQ(intersect(a, a), a);

    // Touchstone of the half-open semantics: rects sharing exactly one edge intersect to
    // empty, no zero-width sliver.
    VISOR_CHECK(
        intersect(a, (RectI{.pos = PointI{10, 0}, .size = SizeI{.w = 5, .h = 5}})).is_empty());
    VISOR_CHECK(
        intersect(a, (RectI{.pos = PointI{0, 10}, .size = SizeI{.w = 5, .h = 5}})).is_empty());

    // Fully disjoint rects and empty operands intersect to empty.
    VISOR_CHECK(
        intersect(a, (RectI{.pos = PointI{100, 100}, .size = SizeI{.w = 1, .h = 1}})).is_empty());
    VISOR_CHECK(
        intersect(a, (RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 0, .h = 10}})).is_empty());

    // Overlap in the negative quadrant clips by the same rules.
    VISOR_CHECK_EQ(intersect(RectI{.pos = PointI{-10, -10}, .size = SizeI{.w = 12, .h = 12}}, a),
                   (RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 2, .h = 2}}));

    // Bounding box: the smallest rect covering both operands.
    VISOR_CHECK_EQ(bounding_box(a, b),
                   (RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 15, .h = 15}}));
    VISOR_CHECK_EQ(bounding_box(a, inner), a); // containment keeps the outer
    VISOR_CHECK_EQ(bounding_box(a, a), a);
    VISOR_CHECK_EQ(bounding_box(neg, a),
                   (RectI{.pos = PointI{-5, -5}, .size = SizeI{.w = 15, .h = 15}}));

    // Point/Size arithmetic: pos + size is the exclusive bottom-right corner, minus walks back.
    VISOR_CHECK_EQ(a.pos + a.size, (PointI{10, 10}));
    VISOR_CHECK_EQ(offset((PointI{1, 2}), SizeI{.w = 3, .h = 4}), (PointI{4, 6}));
    VISOR_CHECK_EQ((PointI{4, 6} - SizeI{.w = 3, .h = 4}), (PointI{1, 2}));
    // Content insets collapse instead of wrapping or producing negative sizes.
    VISOR_CHECK_EQ(visor::base::inset(a, 2), (RectI{.pos = {2, 2}, .size = {.w = 6, .h = 6}}));
    VISOR_CHECK(visor::base::inset(a, 6).is_empty());
    VISOR_CHECK_EQ(visor::base::inset(a, -1), a);
    constexpr visor::base::Rect<unsigned> unsigned_rect{.pos = {0, 0}, .size = {.w = 1, .h = 2}};
    static_assert(visor::base::inset(unsigned_rect, 2U).is_empty());
}
