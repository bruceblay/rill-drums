// Copyright (c) 2026 Bruce Blay
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "Samples.h"

// A bounded, deterministic generative drum machine shared by firmware and
// audition. No allocation, locks, or transcendental functions in the
// per-sample path. Kick and Tom are live-synthesized (pitch-swept sine);
// Snare, Closed/Open Hat, Clap and Rim play back pre-rendered one-shot
// samples (see Samples.h) rather than being synthesized from noise, since
// procedural noise synthesis -- real-time or offline, however filtered or
// layered -- kept reading as harsh and cheap. Driven by an evolving
// sixteen-step groove.
namespace kit {
constexpr uint32_t rate = 32000;
constexpr float pi = 3.14159265358979323846f;
constexpr unsigned steps = 16;
enum Voice : unsigned { Kick = 0, Snare, ClosedHat, OpenHat, Clap, Tom, Rim, voiceCount };
// Bar-length "punch" effects, semi-randomly punched in on the master mix, in
// the spirit of the Pocket Operator / EP-133 punch-in FX. None is not a
// choosable effect, only the resting state.
enum Punch : unsigned { PunchNone = 0, PunchBitcrush, PunchLoFi,
                         PunchDubEcho, PunchFeedback, PunchOctaveDown, punchCount };

class Engine {
  struct Channel {
    bool active = false;
    uint32_t age = 0, duration = 0;
    float freq = 0, freqEnd = 0, freqDecay = 1, phase = 0;
    float toneAmp = 0, toneDecay = 1;
    float clickAmp = 0, clickDecay = 1;
    // Snare/Hat/Clap/Rim: a pointer straight into the flash-resident sample
    // data (see Samples.h), not copied to RAM.
    const int16_t* sampleData = nullptr;
    uint32_t samplePos = 0, sampleLen = 0;
    float sampleGain = 0;
  };
  struct Character {
    float kickStartHz, kickEndHz, kickPitchTime, kickDecay, kickClick;
    float snareHz, snareDecay, snareNoiseMix, snareColorHz, snareQ;
    float hatColorHz, hatQ, hatClosedDecay, hatOpenDecay, hatMetalMix, hatMetalSpread;
    float clapColorHz, clapQ, clapSpread, clapDecay, clapPulses;
    float tomHz, tomDecay;
    float rimHz, rimDecay, rimColorHz;
    float voiceGain, roomSize, roomMix;
  };
  std::array<Channel, voiceCount> channels{};
  std::array<float, 2049> sine{};
  std::array<uint16_t, voiceCount> pattern{};
  std::array<float, voiceCount> baseVelocity{};
  uint32_t rng, scoreRng = 1, performanceRng = 1, noiseRng = 0x9e3779b9u;
  unsigned character = 0, generation = 0;
  unsigned tempo = 68, activity = 1;
  uint32_t stepSamples = rate * 60 / (68 * 4), swingSamples = 0;
  uint64_t clock = 0, nextStep = 0;
  unsigned stepIndex = 0, bar = 0, mutateAt = 3, fillAt = 5, fillLen = 2, fillKind = 0;
  unsigned accentMode = 0;
  bool sequencerMuted = false;
  uint8_t hitMask = 0;
  float outputRamp = 0, target = 1, level = 0;
  // A short room comb and diffuser, far smaller than a rhythmic delay so the
  // groove keeps its edges; only meant to glue the seven voices together.
  std::array<float, 1601> room{};
  unsigned roomIndex = 0;
  float roomDamping = 0, roomFeedback = 0.28f, roomMix = 0.16f;
  std::array<float, 233> diffuser{};
  unsigned diffuserIndex = 0;
  float dcIn = 0, dcOut = 0;

  // Punch-in FX: one active at a time, on the master mix or bent into new
  // voice triggers, cleared automatically at punchEndAt. Each one keeps
  // moving for the length of its window rather than sitting at one setting.
  uint32_t punchRng = 1;
  unsigned punchType = PunchNone;
  uint64_t punchEndAt = 0, punchStartAt = 0;
  float punchPitchScale = 1, punchPitchTarget = 1, punchFeedbackMul = 1, punchMixAdd = 0;
  float punchCrushCenter = 12, punchCrushRange = 8, punchHeldSample = 0;
  unsigned punchHoldCounter = 0;
  std::array<unsigned, 4> loFiHoldSteps{1, 1, 1, 1};
  // A dub-style feedback delay that runs all the time at a modest level, so
  // the groove always has a little repeat and air to it; the Dub Echo punch
  // is a temporary throw on top of this same line, not a separate on/off
  // effect. Sized to hold a full quarter note even at the slowest tempo.
  std::array<int16_t, 40000> dubDelay{};
  unsigned dubWrite = 0;
  unsigned dubBaseDelaySamples = 8000, dubDelaySamples = 8000;
  float dubDelayCurrent = 8000; // eases toward dubDelaySamples, so throws glide rather than jump
  float dubDamp = 0, dubWobblePhase = 0, dubWobbleStep = 0;
  float dubBaseFeedback = 0.16f, dubBaseMix = 0.06f, dubBaseWobbleDepth = 0.8f;
  float dubThrowFeedback = 0, dubThrowMix = 0, dubThrowWobble = 0;

  uint32_t random() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
  float unit() { return float(random() >> 8) / 16777216.0f; }
  uint32_t scoreRandom() { scoreRng ^= scoreRng << 13; scoreRng ^= scoreRng >> 17; scoreRng ^= scoreRng << 5; return scoreRng; }
  float scoreUnit() { return float(scoreRandom() >> 8) / 16777216.0f; }
  uint32_t punchRandom() { punchRng ^= punchRng << 13; punchRng ^= punchRng >> 17; punchRng ^= punchRng << 5; return punchRng; }
  float punchUnit() { return float(punchRandom() >> 8) / 16777216.0f; }
  float performanceUnit() {
    performanceRng ^= performanceRng << 13; performanceRng ^= performanceRng >> 17; performanceRng ^= performanceRng << 5;
    return float(performanceRng >> 8) / 16777216.0f;
  }
  float noise() {
    noiseRng ^= noiseRng << 13; noiseRng ^= noiseRng >> 17; noiseRng ^= noiseRng << 5;
    return float(int32_t(noiseRng)) / 2147483648.0f;
  }
  float wave(float phase) const {
    unsigned i = unsigned(phase);
    return sine[i] + (sine[i + 1] - sine[i]) * (phase - i);
  }
  const Character& active_() const { return kits()[character]; }
  static const std::array<Character, 7>& kits() {
    // Seven kit characters, one nod to Rill's seven timbres. Only the kick/
    // tom fields and the global voiceGain/roomSize/roomMix are read now;
    // the rest (snare..rim synthesis parameters) are unused now that those
    // voices play back samples (see Samples.h) instead, left in place
    // rather than risk re-editing this table again.
    static const std::array<Character, 7> table{{
      {150, 52, 0.05f, 0.30f, 0.22f,  190, 0.16f, 0.55f, 1400, 0.7f,  1800, 0.5f, 0.05f, 0.28f, 0.25f, 0.35f,  2200, 0.55f, 0.012f, 0.30f, 3,  95, 0.34f,  1400, 0.05f, 5200,  1.0f, 0.032f, 0.14f},
      {130, 40, 0.07f, 0.42f, 0.35f,  205, 0.20f, 0.45f, 1800, 1.0f,  3200, 1.2f, 0.025f, 0.18f, 0.55f, 0.3f,  3400, 1.1f, 0.006f, 0.20f, 2,  80, 0.40f,  1900, 0.04f, 6400,  1.05f, 0.020f, 0.10f},
      {110, 55, 0.04f, 0.26f, 0.10f,  170, 0.24f, 0.78f, 1100, 0.4f,  1200, 0.3f, 0.09f, 0.50f, 0.10f, 0.2f,  1400, 0.3f, 0.022f, 0.42f, 4, 105, 0.30f,  1200, 0.07f, 4600,  0.85f, 0.045f, 0.20f},
      {160, 60, 0.05f, 0.24f, 0.28f,  200, 0.14f, 0.40f, 1500, 0.65f, 2400, 0.6f, 0.04f, 0.22f, 0.40f, 0.55f,  2600, 0.6f, 0.011f, 0.26f, 3, 100, 0.26f,  1600, 0.045f, 5600, 0.95f, 0.025f, 0.12f},
      {140, 48, 0.05f, 0.34f, 0.30f,  230, 0.20f, 0.58f, 2200, 0.9f,  4200, 1.4f, 0.03f, 0.20f, 0.75f, 0.95f,  4000, 1.3f, 0.007f, 0.24f, 2, 90, 0.36f,  2400, 0.05f, 7400,  1.0f, 0.030f, 0.16f},
      {120, 38, 0.08f, 0.48f, 0.14f,  165, 0.28f, 0.42f, 900, 0.5f,   1000, 0.35f, 0.10f, 0.55f, 0.15f, 0.25f,  1200, 0.35f, 0.026f, 0.50f, 4, 70, 0.46f,  900, 0.06f,  3400,  0.9f, 0.048f, 0.22f},
      {145, 50, 0.045f, 0.28f, 0.24f,  215, 0.16f, 0.62f, 1900, 1.15f, 4400, 1.5f, 0.02f, 0.15f, 0.85f, 1.0f, 3800, 1.4f, 0.007f, 0.18f, 3, 88, 0.30f,  2100, 0.04f, 8200,  1.0f, 0.022f, 0.11f},
    }};
    return table;
  }
  static uint16_t euclid(unsigned pulses, unsigned n, unsigned rotation) {
    pulses = std::min(pulses, n);
    uint16_t bits = 0;
    for (unsigned i = 0; i < n; ++i) if ((i * pulses) % n < pulses) bits |= uint16_t(1u << i);
    rotation %= n;
    return uint16_t(((bits >> rotation) | (bits << (n - rotation))) & ((1u << n) - 1));
  }
  // Euclid alone is maximally even, which is one specific rhythmic flavor.
  // Using it for every voice on every generation gave each groove the same
  // underlying skeleton no matter which kit was on top, so three more ways
  // of filling the sixteen steps sit alongside it.
  enum Style : unsigned { StyleEuclid = 0, StyleSyncopated, StyleClustered, StyleResponse };
  // Pulses pushed onto the sixteenths between the four main beats, so the
  // voice pulls against the meter instead of landing on it. The stride is
  // coprime with the table length, so positions never repeat.
  uint16_t syncopated(unsigned pulses) {
    static const unsigned offbeats[12] = {3, 7, 11, 15, 1, 5, 9, 13, 2, 6, 10, 14};
    static const unsigned strides[4] = {1, 5, 7, 11};
    unsigned start = scoreRandom() % 12, stride = strides[scoreRandom() % 4];
    uint16_t bits = 0;
    for (unsigned i = 0; i < pulses && i < 12; ++i)
      bits |= uint16_t(1u << offbeats[(start + i * stride) % 12]);
    return bits;
  }
  // A tight run of hits then open space, rather than an even spread. Every
  // few hits it jumps and starts a second cluster somewhere else.
  uint16_t clustered(unsigned pulses) {
    uint16_t bits = 0;
    unsigned at = scoreRandom() % steps, gap = 1 + scoreRandom() % 2;
    for (unsigned i = 0; i < pulses; ++i) {
      bits |= uint16_t(1u << (at % steps));
      at += gap;
      if (i && i % 3 == 0) at += 2 + scoreRandom() % 5;
    }
    return bits;
  }
  // Answers another voice: never on top of the call, and preferring the step
  // straight after one of its hits.
  uint16_t response(uint16_t call, unsigned pulses) {
    uint16_t bits = 0;
    unsigned start = scoreRandom() % steps;
    for (unsigned t = 0; t < steps && pulses; ++t) {
      unsigned s = (start + t) % steps;
      if ((call >> s) & 1u) continue;
      bool after = (call >> ((s + steps - 1) % steps)) & 1u;
      if (!after && scoreUnit() > 0.35f) continue;
      bits |= uint16_t(1u << s);
      --pulses;
    }
    return bits;
  }
  // Weighted so Euclid stays the most common of the four; it is the most
  // reliably musical, the others supply the contrast.
  unsigned pickStyle(unsigned v) {
    unsigned roll = scoreRandom() % 100;
    if (v == Kick) { // no call to answer, and it stays the most grounded voice
      if (roll < 55) return StyleEuclid;
      if (roll < 80) return StyleClustered;
      return StyleSyncopated;
    }
    if (roll < 40) return StyleEuclid;
    if (roll < 62) return StyleSyncopated;
    if (roll < 80) return StyleClustered;
    return StyleResponse;
  }
  // Voice -> column in samples::kit[character][...].
  static unsigned sampleColumn(unsigned v) {
    switch (v) {
      case Snare: return 0; case ClosedHat: return 1; case OpenHat: return 2;
      case Clap: return 3; default: return 4; // Rim
    }
  }
  void triggerSample(Channel& c, unsigned v, float velocity) {
    const samples::Clip& clip = samples::kit[character][sampleColumn(v)];
    c.sampleData = clip.data; c.sampleLen = clip.length; c.samplePos = 0;
    c.sampleGain = velocity;
    c.duration = clip.length;
  }
  void trigger(unsigned v, float velocity) {
    const Character& k = active_();
    Channel& c = channels[v];
    c.active = true; c.age = 0;
    switch (v) {
      case Kick:
        c.duration = uint32_t(k.kickDecay * rate * 1.4f);
        c.freq = k.kickStartHz * punchPitchScale; c.freqEnd = k.kickEndHz * punchPitchScale; c.phase = 0;
        c.freqDecay = std::exp(-1.0f / (rate * k.kickPitchTime));
        c.toneDecay = std::exp(-1.0f / (rate * k.kickDecay));
        c.toneAmp = velocity; c.clickAmp = k.kickClick * velocity;
        c.clickDecay = std::exp(-1.0f / (rate * 0.003f));
        break;
      case Snare:
        triggerSample(c, v, velocity);
        break;
      case ClosedHat:
        triggerSample(c, v, velocity);
        // The classic choke: a closed hat silences a still-ringing open hat.
        channels[OpenHat].active = false; channels[OpenHat].duration = 0;
        break;
      case OpenHat:
        triggerSample(c, v, velocity);
        break;
      case Clap:
        triggerSample(c, v, velocity);
        break;
      case Tom:
        c.duration = uint32_t(k.tomDecay * rate * 1.4f);
        c.freq = k.tomHz * punchPitchScale; c.freqEnd = k.tomHz * 0.82f * punchPitchScale; c.phase = 0;
        c.freqDecay = std::exp(-1.0f / (rate * 0.06f));
        c.toneDecay = std::exp(-1.0f / (rate * k.tomDecay));
        c.toneAmp = velocity;
        break;
      case Rim:
        triggerSample(c, v, velocity);
        break;
    }
    hitMask = uint8_t(hitMask | (1u << v));
  }
  void composeVoice(unsigned v, unsigned pulses, unsigned style) {
    switch (style) {
      case StyleSyncopated: pattern[v] = syncopated(pulses); break;
      case StyleClustered: pattern[v] = clustered(pulses); break;
      case StyleResponse: pattern[v] = response(pattern[Kick], pulses); break;
      default: pattern[v] = euclid(pulses, steps, scoreRandom() % steps); break;
    }
  }
  void composeGroove() {
    static const unsigned pulseRange[voiceCount][2] = {
      {2, 5}, {2, 4}, {6, 12}, {1, 3}, {1, 2}, {0, 0}, {1, 4}};
    for (unsigned v = 0; v < voiceCount; ++v) {
      if (v == Tom) { pattern[v] = 0; continue; } // Tom speaks only in fills.
      unsigned span = pulseRange[v][1] - pulseRange[v][0];
      unsigned pulses = pulseRange[v][0] + (span ? scoreRandom() % (span + 1) : 0);
      // Kick is voice 0, so it is always composed before any voice that
      // might answer it.
      composeVoice(v, pulses, pickStyle(v));
    }
    // An open hat only rings where the closed hat leaves room for it.
    pattern[OpenHat] &= uint16_t(~pattern[ClosedHat]);
    static const float velocities[voiceCount] = {0.95f, 0.85f, 0.85f, 0.85f, 0.85f, 0.80f, 0.85f};
    for (unsigned v = 0; v < voiceCount; ++v) baseVelocity[v] = velocities[v];
  }
  void mutateGroove() {
    unsigned v = scoreRandom() % voiceCount;
    if (v == Tom) v = (v + 1) % voiceCount;
    unsigned move = scoreRandom() % 4;
    if (move == 0) {
      // Nudge the phase of an existing idea rather than replacing it.
      unsigned rotation = 1 + scoreRandom() % (steps - 1);
      pattern[v] = uint16_t(((pattern[v] << rotation) | (pattern[v] >> (steps - rotation))) & 0xffffu);
    } else if (move == 1) {
      static const unsigned pulseRange[voiceCount][2] = {
        {2, 5}, {2, 4}, {6, 12}, {1, 3}, {1, 2}, {0, 0}, {1, 4}};
      unsigned span = pulseRange[v][1] - pulseRange[v][0];
      unsigned pulses = pulseRange[v][0] + (span ? scoreRandom() % (span + 1) : 0);
      composeVoice(v, pulses, pickStyle(v));
    } else if (move == 2) {
      activity = (activity + 1 + scoreRandom() % 3) % 4;
    } else {
      // Swing wanders instead of sitting at one fixed amount for the whole
      // variation: usually eases back toward straight time, occasionally
      // settles on a touch of shuffle for a while, "here and there" rather
      // than constant. Kept light on purpose -- much more than this reads
      // as sloppy timing rather than feel.
      float targetFrac = (scoreRandom() % 5 == 0) ? 0.0f : scoreUnit() * 0.09f;
      swingSamples = uint32_t(stepSamples * targetFrac);
    }
    pattern[OpenHat] &= uint16_t(~pattern[ClosedHat]);
  }
  // Picks the next fill's timing, length and shape together, so a fill
  // reads as one idea (a short tom roll, an alternating tom/rim turnaround,
  // or a snare-and-rim run-up) rather than the same fixed two-step tag
  // every time.
  void scheduleFill() {
    fillAt = bar + 3 + scoreRandom() % 5;
    fillLen = 1 + scoreRandom() % 4;
    fillKind = scoreRandom() % 3;
  }
  void stepClock() {
    if (clock < nextStep) return;
    float activityGain = activity == 0 ? 0.6f : activity == 3 ? 1.15f : 0.85f;
    float skipChance = activity == 0 ? 0.35f : activity == 3 ? 0.0f : 0.10f;
    bool fillStep = bar == fillAt && stepIndex >= steps - fillLen;
    unsigned posInFill = fillStep ? stepIndex - (steps - fillLen) : 0;
    unsigned stepsLeft = steps - stepIndex;
    if (!sequencerMuted) for (unsigned v = 0; v < voiceCount; ++v) {
      bool sounds = (pattern[v] >> stepIndex) & 1u;
      bool fillForced = false;
      if (fillStep) {
        if (fillKind == 0) {
          // A tom roll through the whole fill window, capped by a rim stinger.
          if (v == Tom) fillForced = true;
          else if (v == Rim && stepsLeft == 1) fillForced = true;
        } else if (fillKind == 1) {
          // Tom and rim alternate across the window.
          if (v == Tom && posInFill % 2 == 0) fillForced = true;
          else if (v == Rim && posInFill % 2 == 1) fillForced = true;
        } else {
          // A snare run-up with a tom/rim landing on the downbeat.
          if (v == Snare && posInFill % 2 == 1) fillForced = true;
          else if (v == Rim && stepsLeft <= 2) fillForced = true;
          else if (v == Tom && stepsLeft == 1) fillForced = true;
        }
      }
      if (fillForced) sounds = true;
      if (!sounds) continue;
      if (v != Tom && v != Rim && !fillForced && scoreUnit() < skipChance) continue;
      float accent = accentFor(stepIndex);
      // Fills build toward the downbeat instead of sitting at one level.
      float fillSwell = fillStep ? 0.9f + 0.45f * (float(posInFill + 1) / fillLen) : 1.0f;
      float velocity = baseVelocity[v] * accentActivity(activityGain) * accent * fillSwell * (0.85f + 0.30f * performanceUnit());
      trigger(v, std::min(1.0f, velocity));
    }
    uint32_t swing = (stepIndex % 2 == 1) ? swingSamples : 0;
    nextStep += stepSamples + swing;
    if (++stepIndex == steps) {
      stepIndex = 0; ++bar;
      if (bar >= mutateAt) { mutateGroove(); mutateAt = bar + 2 + scoreRandom() % 4; }
      if (bar > fillAt) scheduleFill();
      maybePunch();
    }
  }
  static float accentActivity(float gain) { return gain; }
  // Where the weight of the bar falls. Always accenting the four made every
  // pattern read through the same metric lens however it was composed.
  float accentFor(unsigned step) const {
    switch (accentMode) {
      case 1: return step % 4 == 2 ? 1.0f : 0.80f; // weight off the downbeat
      case 2: return step % 3 == 0 ? 1.0f : 0.80f; // three against four
      default: return step % 4 == 0 ? 1.0f : 0.82f;
    }
  }
  float punchProgress() const {
    if (punchEndAt <= punchStartAt) return 1;
    return std::min(1.0f, float(clock - punchStartAt) / float(punchEndAt - punchStartAt));
  }
  void maybePunch() {
    if (punchType != PunchNone || punchUnit() > 0.30f) return;
    punchType = 1 + punchRandom() % (punchCount - 1);
    punchStartAt = clock;
    punchEndAt = clock + uint64_t(stepSamples) * steps; // most punches last one bar
    switch (punchType) {
      case PunchBitcrush:
        punchCrushCenter = 9.0f + punchUnit() * 5.0f;
        punchCrushRange = 6.0f + punchUnit() * 10.0f;
        break;
      case PunchLoFi:
        for (auto& n : loFiHoldSteps) n = 2 + punchRandom() % 6;
        punchHoldCounter = 0;
        break;
      case PunchDubEcho: {
        // A temporary throw on the always-on dub line: a longer tap, more
        // feedback, more wobble, eased in via punchProgress() in sample().
        unsigned division = 2 + punchRandom() % 3; // eighth, dotted-eighth or quarter note
        dubDelaySamples = std::min<unsigned>(dubDelay.size() - 1, stepSamples * division);
        dubThrowFeedback = 0.30f + punchUnit() * 0.28f;
        dubThrowMix = 0.22f + punchUnit() * 0.18f;
        dubThrowWobble = 2.0f + punchUnit() * 4.0f;
        punchEndAt = clock + uint64_t(stepSamples) * steps * (1 + punchRandom() % 2); // one or two bars to breathe
        break;
      }
      case PunchFeedback:
        punchFeedbackMul = 1.6f + punchUnit() * 0.9f;
        punchMixAdd = 0.16f + punchUnit() * 0.16f;
        break;
      case PunchOctaveDown:
        // Only Kick and Tom still have a pitch to bend; the sample-based
        // voices don't, so this now affects a smaller, honest scope.
        punchPitchTarget = 0.5f;
        break;
      default: break;
    }
  }
  float applyPunch(float in) {
    // The pitch bend eases toward its target continuously, gliding back to
    // normal after the window closes rather than snapping.
    punchPitchScale += (punchPitchTarget - punchPitchScale) / (rate * 0.18f);
    if (punchType == PunchNone) return in;
    float out = in;
    float progress = punchProgress();
    switch (punchType) {
      case PunchBitcrush: {
        // A slow wobble in quantization depth so it chatters instead of
        // sitting at one flat crush amount.
        float levels = punchCrushCenter + punchCrushRange * 0.5f * (1 + std::sin(progress * 4 * pi));
        out = std::round(in * levels) / levels;
        break;
      }
      case PunchLoFi: {
        unsigned band = std::min<unsigned>(3, unsigned(progress * 4));
        unsigned holdN = loFiHoldSteps[band];
        if (punchHoldCounter == 0) punchHeldSample = in;
        punchHoldCounter = (punchHoldCounter + 1) % holdN;
        out = punchHeldSample;
        break;
      }
      default: break; // Feedback, octave-down and the dub throw act upstream, not here.
    }
    if (clock >= punchEndAt) {
      punchType = PunchNone;
      punchFeedbackMul = 1; punchMixAdd = 0; punchPitchTarget = 1;
      dubDelaySamples = dubBaseDelaySamples;
      dubThrowFeedback = dubThrowMix = dubThrowWobble = 0;
    }
    return out;
  }

 public:
  explicit Engine(uint32_t seed = 0x64756d73) : rng(seed ? seed : 1) {
    for (unsigned i = 0; i <= 2048; ++i) sine[i] = std::sin(2 * pi * i / 2048);
    generate();
  }
  void seed(uint32_t value) { rng = value ? value : 1; generation = 0; generate(); }
  void generate() {
    character = generation ? (character + 1 + random() % 6) % 7 : random() % 7;
    ++generation;
    tempo = 54 + random() % 39;
    stepSamples = rate * 60 / (tempo * 4);
    // Light and occasional by design -- swing also wanders bar to bar in
    // mutateGroove() rather than staying fixed for the whole variation.
    swingSamples = (random() % 4 == 0) ? uint32_t(stepSamples * (unit() * 0.09f)) : 0;
    scoreRng = (rng ^ 0x51ed270bu) | 1u;
    performanceRng = (rng ^ 0xa341316cu) | 1u;
    punchRng = (rng ^ 0xc2b2ae35u) | 1u;
    punchType = PunchNone; punchPitchScale = 1; punchFeedbackMul = 1; punchMixAdd = 0;
    dubThrowFeedback = dubThrowMix = dubThrowWobble = 0;
    activity = scoreRandom() % 3;
    unsigned accentRoll = random() % 4; // the plain four still half the time
    accentMode = accentRoll == 0 ? 1 : accentRoll == 1 ? 2 : 0;
    stepIndex = 0; bar = 0;
    mutateAt = 2 + scoreRandom() % 4;
    scheduleFill();
    nextStep = clock;
    const Character& k = active_();
    roomFeedback = 0.20f + unit() * 0.14f;
    roomMix = k.roomMix * (0.8f + unit() * 0.4f);
    unsigned roomLen = unsigned(std::min<float>(room.size() - 1, rate * k.roomSize));
    roomIndex = roomIndex % std::max(1u, roomLen);
    // An always-on dub delay, synced to an eighth note by default, kept low
    // enough to glue the mix without being identifiable as its own effect.
    dubBaseDelaySamples = std::min<unsigned>(dubDelay.size() - 1, stepSamples * 2);
    dubDelaySamples = dubBaseDelaySamples;
    dubDelayCurrent = float(dubBaseDelaySamples);
    dubBaseFeedback = 0.13f + unit() * 0.07f;
    dubBaseMix = 0.045f + unit() * 0.035f;
    dubBaseWobbleDepth = 0.5f + unit() * 0.6f;
    dubWobbleStep = 2 * pi * (0.15f + unit() * 0.25f) / rate;
    dubWobblePhase = 0; dubDamp = 0; dubWrite = 0; dubDelay.fill(0);
    composeGroove();
  }
  void newVariation() { generate(); }
  uint32_t displayInfo() const {
    return (generation << 21) | (punchType << 18) | (activity << 16) | (character << 13) | (bar << 7) | tempo;
  }
  unsigned variation() const { return generation; }
  unsigned bpm() const { return tempo; }
  unsigned currentPunch() const { return punchType; }
  unsigned kitCharacter() const { return character; }
  unsigned activityState() const { return activity; }
  unsigned currentStep() const { return stepIndex; }
  unsigned barCount() const { return bar; }
  uint64_t frames() const { return clock; }
  uint8_t drainHits() { uint8_t m = hitMask; hitMask = 0; return m; }
  void setPlaying(bool playing) { target = playing ? 1.0f : 0.0f; }
  // For the on-device dev/test mode: trigger one voice deliberately and set
  // the kit character directly, rather than waiting on the generative
  // groove and its near-random kit rotation to land on the right one.
  void devTrigger(unsigned voice, float velocity = 1.0f) { trigger(voice % voiceCount, velocity); }
  void setKitCharacter(unsigned index) { character = index % unsigned(kits().size()); }
  void setSequencerMuted(bool muted) { sequencerMuted = muted; }
  unsigned activeVoices() const {
    unsigned n = 0; for (auto& c : channels) n += c.active; return n;
  }
  uint16_t voicePattern(unsigned v) const { return pattern[v]; }
  float sample() {
    stepClock(); ++clock;
    float dry = 0;
    for (unsigned v = 0; v < voiceCount; ++v) {
      Channel& c = channels[v];
      if (!c.active) continue;
      float tone = 0, sampleOut = 0, click = 0;
      if (v == Kick || v == Tom) {
        c.freq += (c.freqEnd - c.freq) * (1 - c.freqDecay);
        c.phase += c.freq * 2048.0f / rate;
        if (c.phase >= 2048) c.phase -= 2048;
        tone = wave(c.phase) * c.toneAmp;
        c.toneAmp *= c.toneDecay;
      } else if (c.samplePos < c.sampleLen) {
        // Snare/Hats/Clap/Rim: playback only, straight from flash. No live
        // noise synthesis for these voices -- see CLAUDE.md.
        sampleOut = (float(c.sampleData[c.samplePos]) / 32768.0f) * c.sampleGain;
        ++c.samplePos;
      }
      if (v == Kick) {
        // Kick's own short click transient is the one remaining live noise
        // generator in this file, and it's scoped to Kick alone.
        click = noise() * c.clickAmp;
        c.clickAmp *= c.clickDecay;
      }
      dry += (tone + sampleOut + click);
      if (++c.age >= c.duration) c.active = false;
    }
    dry *= active_().voiceGain * 0.5f;
    // The feedback punch swells in and back out over its window rather than
    // stepping to a flat boosted level and cutting off abruptly.
    float feedbackSwell = punchType == PunchFeedback ? std::sin(punchProgress() * pi) : 0;
    unsigned roomLen = unsigned(std::min<float>(room.size() - 1, rate * active_().roomSize));
    roomLen = std::max(200u, roomLen);
    float delayed = room[roomIndex % roomLen];
    roomDamping += 0.30f * (delayed - roomDamping);
    room[roomIndex % roomLen] = dry * 0.5f + roomDamping * roomFeedback * (1 + (punchFeedbackMul - 1) * feedbackSwell);
    ++roomIndex;
    float a = diffuser[diffuserIndex];
    diffuser[diffuserIndex] = delayed + a * 0.5f;
    float wet = a - diffuser[diffuserIndex] * 0.5f;
    if (++diffuserIndex == diffuser.size()) diffuserIndex = 0;
    float mix = dry + wet * (roomMix + punchMixAdd * feedbackSwell);
    float clean = mix - dcIn + 0.999f * dcOut;
    dcIn = mix; dcOut = clean;
    // A dub-style delay that is always present at a modest level; the Dub
    // Echo punch temporarily throws its feedback, mix, wobble and tap length
    // up (eased by punchProgress()) rather than switching a separate effect
    // on and off.
    float dubThrow = punchType == PunchDubEcho ? std::sin(punchProgress() * pi) : 0;
    dubDelayCurrent += (float(dubDelaySamples) - dubDelayCurrent) / (rate * 0.25f);
    dubWobblePhase += dubWobbleStep; if (dubWobblePhase >= 2 * pi) dubWobblePhase -= 2 * pi;
    float dubWobbleNow = dubBaseWobbleDepth + dubThrowWobble * dubThrow;
    float dubReadPos = std::max(1.0f, std::min(float(dubDelay.size() - 2),
                          dubDelayCurrent + std::sin(dubWobblePhase) * dubWobbleNow));
    unsigned dubWhole = unsigned(dubReadPos);
    float dubFrac = dubReadPos - dubWhole;
    unsigned dub0 = unsigned((dubWrite + dubDelay.size() - dubWhole) % dubDelay.size());
    unsigned dub1 = (dub0 + dubDelay.size() - 1) % dubDelay.size();
    float dubTapped = (dubDelay[dub0] + (dubDelay[dub1] - dubDelay[dub0]) * dubFrac) / 32768.0f;
    // Each repeat darkens a little more, the way a tape echo's head loses treble.
    dubDamp += 0.35f * (dubTapped - dubDamp);
    float dubFeedbackNow = dubBaseFeedback + dubThrowFeedback * dubThrow;
    float dubWriteValue = clean + dubDamp * dubFeedbackNow;
    dubDelay[dubWrite] = int16_t(std::max(-0.98f, std::min(0.98f, dubWriteValue)) * 32767);
    if (++dubWrite == dubDelay.size()) dubWrite = 0;
    float dubMixNow = dubBaseMix + dubThrowMix * dubThrow;
    clean += dubTapped * dubMixNow;
    float processed = applyPunch(clean);
    level += (target - level) / (rate * 0.15f);
    outputRamp = std::min(1.0f, outputRamp + 1.0f / (rate * 0.08f));
    float x = processed * level * outputRamp * 1.7f;
    return x / (1 + std::abs(x));
  }
  void render(int16_t* output, unsigned count) {
    for (unsigned i = 0; i < count; ++i) output[i] = int16_t(sample() * 32767);
  }
};
}
