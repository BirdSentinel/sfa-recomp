#include "src/ppc_math_fixes.h"
#include <cmath>
#include <initializer_list>
#include <cstdio>
#include <cstdlib>

int main() {
  const float inf = std::numeric_limits<float>::infinity();
  float in[4] = {-inf, inf, std::numeric_limits<float>::quiet_NaN(), 0};
  float out[4];
  snupi::Exp2Estimate(out, in);
  if (out[0] != 0 || out[1] != inf || !std::isnan(out[2]) || out[3] != 1) return 1;
  for (float x : {-1000.f, -200.f, -128.f, -127.f, -126.01f}) {
    for (auto& lane : in) lane = x;
    snupi::Exp2Estimate(out, in);
    for (auto lane : out) if (lane != 0 || std::signbit(lane)) return 2;
  }
  for (int i = -504; i < 512; ++i) {
    const float x = i * 0.25f;
    for (auto& lane : in) lane = x;
    snupi::Exp2Estimate(out, in);
    const float reference = std::exp2(x);
    for (auto lane : out) if (!std::isfinite(lane) || std::abs(lane / reference - 1) > 0.000002f) return 3;
  }
  for (auto& lane : in) lane = 128;
  snupi::Exp2Estimate(in, in); // Register aliasing is legal.
  for (auto lane : in) if (lane != inf) return 4;
  std::puts("Vector exponential edge cases, normal range, and aliasing passed.");
}

