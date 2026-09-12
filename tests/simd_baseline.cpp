#include <doctest/doctest.h>

#include "pjh_json/detail/simd_info.hpp"
#include "pjh_json/document.hpp"

using namespace pjh::json;

// Pins the baseline ISA facts the byte scanners rely on: with no ISA flags
// xsimd selects SSE2 on x86-64 / NEON on arm64, both 16-byte batches. The
// lower bound guards against a narrower batch than the scanners ever
// supported; the upper bound guards the uint64_t batch mask; the padding
// identity keeps 2x headroom under kPaddingWidth.
TEST_CASE("SIMD: baseline batch width")
{
    const std::size_t width = simd_batch_width();
    CHECK(width >= 16);
    CHECK(width <= 64);
    CHECK(2 * width <= kPaddingWidth);

    const char *arch = simd_baseline_arch_name();
    REQUIRE(arch != nullptr);
    CHECK(arch[0] != '\0');
}
