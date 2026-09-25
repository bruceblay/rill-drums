// Copyright (c) 2026 Bruce Blay
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/Kit.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>

// The ensemble lines each device up by barPhase(), so it has to name the bar
// the grid is really in: the downbeat reads as zero, and the bar's origin
// stays put however much swing the groove picks up along the way.
static void checkBarPhase() {
  unsigned swungSeeds = 0;
  for (uint32_t seed = 1; seed <= 24; ++seed) {
    kit::Engine engine(seed);
    const uint32_t span = engine.barSamples();
    int64_t origin = -1;
    unsigned previousStep = engine.currentStep(), downbeats = 0;
    for (unsigned i = 0; i < kit::rate * 120; ++i) {
      engine.sample();
      const int64_t here = int64_t(engine.frames()) - engine.barPhase();
      const int64_t folded = ((here % span) + span) % span;
      if (origin < 0) origin = folded;
      assert(folded == origin);  // no bar ever runs long
      const unsigned step = engine.currentStep();
      if (previousStep == 0 && step == 1) { assert(engine.barPhase() < 4); ++downbeats; }
      previousStep = step;
    }
    assert(downbeats > 20);
    swungSeeds += engine.swing() > 0;
  }
  assert(swungSeeds > 0);  // the check has to have met some swing
  std::cout << "bar phase: downbeat reads zero, swung bars keep their length (" << swungSeeds << " seeds swung)\n";
}

// The ensemble's loop, as main.cpp runs it: every 120 ms, trim a quarter of
// the gap between the shared bar and barPhase(). Against a bar well off the
// groove's own, the groove has to settle onto it and stay, swing and all, and
// through taps: a new groove waits for the downbeat and keeps the grid.
static void checkTrimSettles() {
  for (uint32_t seed : {1u, 5u, 11u}) {
    kit::Engine engine(seed);
    engine.followTempo(engine.bpm());  // as the ensemble does, every 120 ms
    const unsigned first = engine.variation();
    const int64_t span = engine.barSamples(), check = kit::rate * 120 / 1000;
    const int64_t origin = span / 3;
    int64_t worst = 0;
    unsigned previousStep = engine.currentStep(), previousVariation = first;
    for (int64_t i = 1; i <= int64_t(kit::rate) * 40; ++i) {
      engine.sample();
      if (i > int64_t(kit::rate) * 15 && i % (kit::rate * 3) == 0) engine.newVariation();
      // A new groove only ever begins on a downbeat.
      if (engine.variation() != previousVariation) {
        assert(previousStep == kit::steps - 1 && engine.currentStep() == 0);
        previousVariation = engine.variation();
      }
      previousStep = engine.currentStep();
      if (i % check) continue;
      int64_t want = ((int64_t(engine.frames()) - origin) % span + span) % span;
      int64_t error = ((want - int64_t(engine.barPhase())) % span + span) % span;
      if (error > span / 2) error -= span;
      engine.trimGrid(int32_t(error / 4));
      if (i > int64_t(kit::rate) * 15) worst = std::max(worst, std::llabs(error));
    }
    assert(worst < kit::rate * 2 / 1000);
    assert(engine.variation() > first + 3);  // the taps did land
    std::cout << "seed " << seed << ": settles on the shared bar, worst " << worst << " samples after 15 s, through taps\n";
  }
}

int main() {
  checkBarPhase();
  checkTrimSettles();
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
