#pragma once
#include <limits>
#include <simde/x86/sse4.1.h>

namespace snupi {
// ReXGlue 0.10.0's generated vexptefp polynomial assumes finite inputs in
// the normal exponent range. In particular, -infinity - -infinity yields
// NaN. Muted Wwise channels legitimately pass -infinity and require zero.
inline void Exp2Estimate(float* output, const float* input) {
  const auto original = simde_mm_loadu_ps(input);
  const auto x = simde_mm_min_ps(simde_mm_max_ps(original,
      simde_mm_set1_ps(-126.0f)), simde_mm_set1_ps(127.99999237060546875f));
  const auto n = simde_mm_round_ps(x,
      SIMDE_MM_FROUND_TO_NEG_INF | SIMDE_MM_FROUND_NO_EXC);
  const auto f = simde_mm_sub_ps(x, n);
  auto p = simde_mm_set1_ps(1.8775767e-3f);
  p = simde_mm_add_ps(simde_mm_mul_ps(p, f), simde_mm_set1_ps(8.9893397e-3f));
  p = simde_mm_add_ps(simde_mm_mul_ps(p, f), simde_mm_set1_ps(5.5826318e-2f));
  p = simde_mm_add_ps(simde_mm_mul_ps(p, f), simde_mm_set1_ps(2.4015361e-1f));
  p = simde_mm_add_ps(simde_mm_mul_ps(p, f), simde_mm_set1_ps(6.9315308e-1f));
  p = simde_mm_add_ps(simde_mm_mul_ps(p, f), simde_mm_set1_ps(1.0f));
  const auto exponent = simde_mm_slli_epi32(simde_mm_add_epi32(
      simde_mm_cvttps_epi32(n), simde_mm_set1_epi32(127)), 23);
  auto result = simde_mm_mul_ps(p, simde_mm_castsi128_ps(exponent));
  // VMX arithmetic flushes denormal results to zero.
  result = simde_mm_blendv_ps(result, simde_mm_setzero_ps(),
      simde_mm_cmplt_ps(original, simde_mm_set1_ps(-126.0f)));
  result = simde_mm_blendv_ps(result,
      simde_mm_set1_ps(std::numeric_limits<float>::infinity()),
      simde_mm_cmpge_ps(original, simde_mm_set1_ps(128.0f)));
  result = simde_mm_blendv_ps(result, simde_mm_add_ps(original, original),
      simde_mm_cmpunord_ps(original, original));
  simde_mm_storeu_ps(output, result);
}
}
