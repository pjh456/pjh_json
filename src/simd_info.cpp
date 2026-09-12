#include "pjh_json/detail/simd_info.hpp"
#include "pjh_json/document.hpp"

#include <cstdint>

#include <xsimd/xsimd.hpp>

namespace pjh::json
{
    namespace
    {
        using batch_type = xsimd::batch<uint8_t>;

        /*
         * Baseline ISA invariants, pinned in the one TU that can see xsimd
         * without leaking it into a public header. These mirror the
         * static_asserts at each scanner call site and make an accidental
         * ISA/flag change fail here too:
         *   - batch_bool::mask() returns uint64_t, so a batch must fit;
         *   - a wide load may overread one batch, so the padding contract
         *     must cover 2x the batch (kPaddingWidth == 128 today).
         */
        static_assert(batch_type::size <= 64, "batch_size too large for uint64_t mask");
        static_assert(2 * batch_type::size <= kPaddingWidth, "padding must keep 2x SIMD batch headroom");
    } // namespace

    std::size_t simd_batch_width() noexcept
    {
        return batch_type::size;
    }

    const char *simd_baseline_arch_name() noexcept
    {
        return xsimd::default_arch::name();
    }

} // namespace pjh::json
