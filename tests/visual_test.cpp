// Copyright (c) 2026 Bruce Blay
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/Pulse.h"
#include <cassert>
#include <memory>
#include <iostream>
uint32_t hash(const pulse::Painting& p) {
  uint32_t value = 2166136261u;
  for (unsigned i = 0; i < 240 * 135; ++i) value = (value ^ p.pixels()[i]) * 16777619u;
  return value;
}
int main() {
  auto a = std::unique_ptr<pulse::Painting>(new pulse::Painting(17));
  auto b = std::unique_ptr<pulse::Painting>(new pulse::Painting(17));
  a->render(0, 0, 0); b->render(0, 0, 0);
  assert(hash(*a) == hash(*b));
  auto initial = hash(*a);
  unsigned seen = 0;
  for (unsigned i = 0; i < 200; ++i) {
    if (i % 20 == 0) { auto old = a->visualFamily(); a->regenerate(); b->regenerate(); assert(old != a->visualFamily()); }
    seen |= 1u << a->visualFamily();
    uint8_t hits = uint8_t(1u << (i % 7));
    a->render(1.0f / 60, hits, i % 16); b->render(1.0f / 60, hits, i % 16);
    assert(hash(*a) == hash(*b));
  }
  assert(seen == 0x39u); // Grid and Radial Grid (bits 1,2) are hidden from rotation for now.
  assert(hash(*a) != initial && a->generation() == 11);
  a->render(0.1f, 0x7f, 3); b->render(0.1f, 0, 3);
  assert(hash(*a) != hash(*b));
  // Every active family animates and reacts to hits; a shake cycles to the next one immediately.
  for (unsigned family : {0u, 3u, 4u, 5u}) {
    a->seed(17); b->seed(17);
    while (a->visualFamily() != family) { a->regenerate(); b->regenerate(); }
    a->render(0, 0, 0); auto start = hash(*a);
    for (unsigned i = 0; i < 90; ++i) { a->render(1.0f / 60, uint8_t(1u << (i % 7)), i % 16); b->render(1.0f / 60, 0, i % 16); }
    assert(hash(*a) != start && hash(*a) != hash(*b));
    auto before = hash(*a); a->regenerate(); a->render(0, 0, 0);
    assert(hash(*a) != before);
  }
  std::cout << "Visual transitions, deterministic seeds, hit response and frame bounds passed\n";
}
