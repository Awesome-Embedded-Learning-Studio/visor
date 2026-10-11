#include <format>

// point_format.hpp contributes the std::formatter specialization for Point: no named symbol
// comes out of it, so the keep pragma tells include-cleaner the include is intentional.
#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/debugging/point_format.hpp" // IWYU pragma: keep
#include "visor/base/geometry/point.hpp"

void run_point_tests() {
    using visor::base::PointD;
    using visor::base::PointI;

    // Same-type arithmetic, scalar mul/div, negation. Macro arguments holding brace
    // initialization need an extra pair of parentheses: the preprocessor balances
    // parentheses only, so commas inside braces would split the argument.
    const PointI a{1, 2};
    const PointI b{3, 4};
    VISOR_CHECK_EQ(a + b, (PointI{4, 6}));
    VISOR_CHECK_EQ(b - a, (PointI{2, 2}));
    VISOR_CHECK_EQ(-a, (PointI{-1, -2}));
    VISOR_CHECK_EQ(3 * a, (PointI{3, 6}));
    VISOR_CHECK_EQ(a * 3, (PointI{3, 6}));
    VISOR_CHECK_EQ(a / 1, a);

    // Compound assignment and equality.
    PointD v{1.5, 2.5};
    v += PointD{2.0, 4.0};
    v /= 2.0;
    VISOR_CHECK_EQ(v, (PointD{1.75, 3.25}));

    // Dot product, plus the explicit converting constructor: int coordinates widen to double.
    VISOR_CHECK_EQ((PointD{1.5, 2.5}.dot(PointD{2.0, 4.0})), 13.0);
    VISOR_CHECK_EQ((PointD{PointI{1, 2}}), (PointD{1.0, 2.0}));

    // Formatting goes through the std::formatter specialization, output is "(x, y)".
    VISOR_CHECK_EQ(std::format("{}", PointI{4, 6}), "(4, 6)");
    VISOR_CHECK_EQ(std::format("{}", v), "(1.75, 3.25)");
}
