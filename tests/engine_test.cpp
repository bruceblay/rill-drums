// Copyright (c) 2026 Bruce Blay
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/Kit.h"
#include <cassert>
#include <iostream>
#include <memory>

int main() {
  unsigned charactersSeen = 0, activitySeen = 0;
  for (uint32_t seed : {1u, 0x64756d73u, 0xffffffffu}) {
    auto engine = std::unique_ptr<kit::Engine>(new kit::Engine(seed));
    float previous = 0, peak = 0, jump = 0;
    double energy = 0;
    for (unsigned i = 0; i < kit::rate * 300; ++i) {
      // Exercise live kit and groove changes, including quick repeated taps.
      if (i && i % (kit::rate * 11) == 0) engine->newVariation();
      if (i % (kit::rate * 11) == kit::rate / 10) engine->newVariation();
      float s = engine->sample();
      assert(std::isfinite(s) && std::abs(s) < 0.9f);
      assert(engine->bpm() >= 54 && engine->bpm() <= 92);
      assert(engine->kitCharacter() < 7);
      assert(engine->activityState() < 4);
      assert(engine->currentStep() < kit::steps);
      assert(engine->currentPunch() < kit::punchCount);
      charactersSeen |= 1u << engine->kitCharacter();
      activitySeen |= 1u << engine->activityState();
      peak = std::max(peak, std::abs(s));
      jump = std::max(jump, std::abs(s - previous));
      previous = s; energy += s * s;
    }
    assert(peak > 0.05f && jump < 1.3f);
    assert(std::sqrt(energy / (kit::rate * 300)) > 0.01);
    engine->setPlaying(false);
    for (unsigned i = 0; i < kit::rate * 8; ++i) previous = engine->sample();
    assert(std::abs(previous) < 0.0001f);
    engine->setPlaying(true);
    energy = 0;
    for (unsigned i = 0; i < kit::rate * 8; ++i) { float s = engine->sample(); energy += s * s; }
    assert(energy > 0.5);
    std::cout << "seed " << seed << ": five-minute stability, headroom, fade/resume passed; peak=" << peak << " jump=" << jump << '\n';
  }
  assert(charactersSeen == 127 && activitySeen == 15);

  auto a = std::unique_ptr<kit::Engine>(new kit::Engine(42));
  auto b = std::unique_ptr<kit::Engine>(new kit::Engine(42));
  int16_t block[512];
  for (unsigned i = 0; i < 300; ++i) {
    a->render(block, 512);
    for (auto s : block) assert(s == int16_t(b->sample() * 32767));
  }
  std::cout << "Seed reproducibility and block rendering passed\n";

  // The open hat must never sound on a step where the closed hat also fires.
  for (uint32_t seed : {1u, 7u, 99u, 12345u}) {
    kit::Engine engine(seed);
    for (unsigned tap = 0; tap < 20; ++tap) {
      assert((engine.voicePattern(kit::OpenHat) & engine.voicePattern(kit::ClosedHat)) == 0);
      engine.newVariation();
    }
  }
  std::cout << "Open hat never collides with closed hat across kit changes\n";

  auto c = std::unique_ptr<kit::Engine>(new kit::Engine(7));
  unsigned firstCharacter = c->kitCharacter();
  uint32_t hitsInAMinute = 0;
  for (unsigned i = 0; i < kit::rate * 60; ++i) { c->sample(); if (c->drainHits()) ++hitsInAMinute; }
  assert(hitsInAMinute > 20); // The groove is audibly alive, not silent or frozen.
  c->newVariation();
  assert(c->variation() == 2 && c->kitCharacter() != firstCharacter);
  std::cout << "New variation advances the kit and groove keeps moving; hits/min=" << hitsInAMinute << "\n";

  // Every punch-in effect eventually fires, and the mix stays bounded through
  // stutters, sweeps, feedback throws and octave drops alike.
  unsigned punchesSeen = 0;
  float punchPeak = 0;
  for (uint32_t seed : {3u, 21u, 555u, 8080u}) {
    kit::Engine engine(seed);
    for (unsigned i = 0; i < kit::rate * 1200; ++i) {
      float s = engine.sample();
      assert(std::isfinite(s) && std::abs(s) < 0.9f);
      if (engine.currentPunch()) { punchesSeen |= 1u << engine.currentPunch(); punchPeak = std::max(punchPeak, std::abs(s)); }
    }
  }
  assert(punchesSeen == 0x3eu); // All five punch types (bits 1..5) fired.
  assert(punchPeak > 0.05f);
  std::cout << "All five punch-in effects fired and stayed within headroom\n";

  // Every one of the seven kit characters must produce audible output on
  // its own. A malformed row in the kit table (e.g. a dropped field
  // shifting every later field, silently zeroing voiceGain) would pass
  // every other test here since the other six kits still make noise; this
  // isolates each one and would catch that class of bug directly.
  for (unsigned target = 0; target < 7; ++target) {
    kit::Engine engine(target + 1);
    while (engine.kitCharacter() != target) engine.newVariation();
    float peak = 0;
    double energy = 0;
    for (unsigned i = 0; i < kit::rate; ++i) {
      float s = engine.sample();
      assert(std::isfinite(s) && std::abs(s) < 0.9f);
      peak = std::max(peak, std::abs(s));
      energy += s * s;
    }
    assert(peak > 0.03f);
    assert(std::sqrt(energy / kit::rate) > 0.003);
  }
  std::cout << "Every kit character produces audible output in isolation\n";
}
