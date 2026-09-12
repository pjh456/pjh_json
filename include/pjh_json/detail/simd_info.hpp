#ifndef INCLUDE_PJH_JSON_DETAIL_SIMD_INFO_HPP
#define INCLUDE_PJH_JSON_DETAIL_SIMD_INFO_HPP

#include <cstddef>

namespace pjh::json
{
    /**
     * @brief SIMD byte-scan width used by the library (bytes per batch)
     *
     * Reports the compile-time `xsimd::batch<uint8_t>::size` of the byte
     * scanners. With no ISA flags this is 16 on x86-64 (SSE2) and arm64
     * (NEON); rebuilding the library with a wider ISA (e.g. -mavx2) raises
     * it. This declaration stays xsimd-free: the definition lives in
     * src/simd_info.cpp, which reaches xsimd through the library's PRIVATE
     * dependency edge. Internal header, not installed.
     *
     * @return Batch width in bytes.
     */
    [[nodiscard]] std::size_t simd_batch_width() noexcept;

    /**
     * @brief Name of xsimd's compile-time default architecture
     *
     * The baseline build (no ISA flags) reports "sse2" on x86-64 and a NEON
     * name on arm64. The returned pointer has static storage duration.
     * Internal header, not installed.
     *
     * @return NUL-terminated architecture name.
     */
    [[nodiscard]] const char *simd_baseline_arch_name() noexcept;

} // namespace pjh::json

#endif // INCLUDE_PJH_JSON_DETAIL_SIMD_INFO_HPP
