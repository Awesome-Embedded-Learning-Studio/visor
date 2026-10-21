#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/submit_plan.hpp"

#include <array>
#include <cstddef>

namespace {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::host::SubmitPlan;

constexpr SizeI kScreen{.w = 20, .h = 20};
constexpr RectI kBounds{.pos = PointI{0, 0}, .size = kScreen};

/// True when cover contains every pixel of area, the plan's one promise put
/// as a question a judgment can ask.
constexpr bool covers(const RectI& cover, const RectI& area) {
    return cover.pos.x <= area.pos.x && cover.pos.y <= area.pos.y &&
           (area.pos.x + area.size.w) <= (cover.pos.x + cover.size.w) &&
           (area.pos.y + area.size.h) <= (cover.pos.y + cover.size.h);
}

/// True when some planned rectangle covers area.
bool some_plan_covers(const SubmitPlan& plan, const RectI& area) {
    for (std::size_t i = 0; i < plan.count(); ++i) {
        if (covers(plan.at(i), area)) {
            return true;
        }
    }
    return false;
}

} // namespace

void run_submit_plan_tests() {
    // Clipping is the door: a push overhanging the bounds keeps only its
    // owned part, a push missing the bounds entirely never joins.
    {
        SubmitPlan plan{kBounds};
        plan.push(RectI{.pos = PointI{15, 0}, .size = SizeI{.w = 10, .h = 8}});
        VISOR_CHECK_EQ(plan.count(), std::size_t{1});
        VISOR_CHECK_EQ(plan.at(0), (RectI{.pos = PointI{15, 0}, .size = SizeI{.w = 5, .h = 8}}));
        plan.push(RectI{.pos = PointI{100, 100}, .size = SizeI{.w = 2, .h = 2}});
        VISOR_CHECK_EQ(plan.count(), std::size_t{1});
        VISOR_CHECK(covers(kBounds, plan.at(0))); // never past the window
    }

    // The strip rebuild's own shape: three stacked bands sharing edges fold
    // into one rectangle, the batch a five-band screen wants to become.
    {
        SubmitPlan plan{kBounds};
        plan.push(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 8}});
        plan.push(RectI{.pos = PointI{0, 8}, .size = SizeI{.w = 20, .h = 8}});
        plan.push(RectI{.pos = PointI{0, 16}, .size = SizeI{.w = 20, .h = 4}});
        VISOR_CHECK_EQ(plan.count(), std::size_t{1});
        VISOR_CHECK_EQ(plan.at(0), (RectI{.pos = PointI{0, 0}, .size = kScreen}));
    }

    // Side by side merges exactly: two halves of one row band become the
    // band, no pixel wasted, because the union is the box.
    {
        SubmitPlan plan{kBounds};
        plan.push(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 20}});
        plan.push(RectI{.pos = PointI{10, 0}, .size = SizeI{.w = 10, .h = 20}});
        VISOR_CHECK_EQ(plan.count(), std::size_t{1});
        VISOR_CHECK_EQ(plan.at(0), (RectI{.pos = PointI{0, 0}, .size = kScreen}));
    }

    // A gap is a fact: rectangles a row apart stay separate, and a rectangle
    // meeting its diagonal neighbor at one corner point stays separate too --
    // no shared edge, no merge.
    {
        SubmitPlan plan{kBounds};
        plan.push(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 5, .h = 5}});
        plan.push(RectI{.pos = PointI{0, 6}, .size = SizeI{.w = 5, .h = 5}});
        VISOR_CHECK_EQ(plan.count(), std::size_t{2});
        plan.push(RectI{.pos = PointI{5, 5}, .size = SizeI{.w = 1, .h = 1}});
        VISOR_CHECK_EQ(plan.count(), std::size_t{3});
    }

    // The cascade: an L of three rectangles folds to one box, each merge
    // growing the resident until no neighbor is left.
    {
        SubmitPlan plan{kBounds};
        plan.push(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 4}});
        plan.push(RectI{.pos = PointI{0, 4}, .size = SizeI{.w = 4, .h = 6}});
        plan.push(RectI{.pos = PointI{10, 0}, .size = SizeI{.w = 4, .h = 4}});
        VISOR_CHECK_EQ(plan.count(), std::size_t{1});
        VISOR_CHECK_EQ(plan.at(0), (RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 14, .h = 10}}));
    }

    // Overflow collapses, the collector's stance: nine disjoint pushes on a
    // capacity-eight list fold the first eight into one box and keep the
    // ninth beside it. Coverage survives the fold -- every pushed rectangle
    // still sits inside some planned one.
    {
        SubmitPlan plan{kBounds};
        std::array<RectI, 9> pushed{};
        for (int i = 0; i < 9; ++i) {
            pushed[static_cast<std::size_t>(i)] =
                RectI{.pos = PointI{i * 2, 0}, .size = SizeI{.w = 1, .h = 1}};
            plan.push(pushed[static_cast<std::size_t>(i)]);
        }
        VISOR_CHECK_EQ(plan.count(), std::size_t{2});
        for (const RectI& area : pushed) {
            VISOR_CHECK(some_plan_covers(plan, area));
        }
        VISOR_CHECK(some_plan_covers(
            plan, RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 15, .h = 1}})); // the box half
        VISOR_CHECK(some_plan_covers(
            plan, RectI{.pos = PointI{16, 0}, .size = SizeI{.w = 1, .h = 1}})); // the newcomer
    }

    // The retire verb: clear empties the plan without ceremony, and a later
    // push starts a fresh batch on the same bounds.
    {
        SubmitPlan plan{kBounds};
        plan.push(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = 4}});
        VISOR_CHECK(!plan.empty());
        plan.clear();
        VISOR_CHECK(plan.empty());
        plan.push(RectI{.pos = PointI{6, 6}, .size = SizeI{.w = 2, .h = 2}});
        VISOR_CHECK_EQ(plan.count(), std::size_t{1});
        VISOR_CHECK_EQ(plan.at(0), (RectI{.pos = PointI{6, 6}, .size = SizeI{.w = 2, .h = 2}}));
    }

    // The pre-open plan: empty bounds clip everything away, so a host built
    // but not yet opened drops instead of batching into nowhere.
    {
        SubmitPlan plan{};
        plan.push(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = 4}});
        VISOR_CHECK(plan.empty());
    }

    // One compile-time run of the whole stance, so the constexpr shape stays
    // honest: strips fold, coverage holds, all inside the bounds.
    {
        constexpr SubmitPlan checked = [] {
            SubmitPlan plan{kBounds};
            plan.push(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 8}});
            plan.push(RectI{.pos = PointI{0, 8}, .size = SizeI{.w = 20, .h = 8}});
            plan.push(RectI{.pos = PointI{0, 16}, .size = SizeI{.w = 20, .h = 4}});
            return plan;
        }();
        static_assert(checked.count() == 1);
        static_assert(checked.at(0) == RectI{.pos = PointI{0, 0}, .size = kScreen});
    }
}
