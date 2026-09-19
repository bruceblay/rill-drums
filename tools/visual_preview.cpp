// Copyright (c) 2026 Bruce Blay
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/Kit.h"
#include "../src/Pulse.h"
#include <fstream>
#include <memory>
#include <cstdlib>
int main(int argc, char** argv) {
  if (argc < 2) return 1;
  uint32_t seed = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 17;
  auto engine = std::unique_ptr<kit::Engine>(new kit::Engine(seed));
  auto p = std::unique_ptr<pulse::Painting>(new pulse::Painting(seed));
  if (argc > 3) {
    unsigned family = unsigned(std::strtoul(argv[3], nullptr, 10)) % pulse::Painting::families;
    while (p->visualFamily() != family) p->regenerate();
  }
  // Advance a few bars of real audio so the visual has drum hits to react to.
  for (unsigned i = 0; i < kit::rate * 6; ++i) {
    engine->sample();
    if ((i % (kit::rate / 60)) == 0) p->render(1.0f / 60, engine->drainHits(), engine->currentStep());
  }
  std::ofstream out(argv[1], std::ios::binary);
  out << "P6\n240 135\n255\n";
  for (unsigned i = 0; i < 240 * 135; ++i) {
    uint16_t c = p->pixels()[i];
    out.put(char(((c >> 11) & 31) * 255 / 31));
    out.put(char(((c >> 5) & 63) * 255 / 63));
    out.put(char((c & 31) * 255 / 31));
  }
  return out ? 0 : 2;
}
