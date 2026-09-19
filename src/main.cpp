// Copyright (c) 2026 Bruce Blay
// SPDX-License-Identifier: GPL-3.0-or-later
#include <M5Unified.h>
#include <atomic>
#include <esp_system.h>
#include "Kit.h"
#include "Pulse.h"
#include "ShakeDetector.h"

// Working title. Display and controls run separately from the audio producer.
static kit::Engine engine;
static pulse::Painting painting;
static ShakeDetector shake;
static bool infoVisible = false, audioFailed = false;
static uint32_t infoAt = 0, worstVisualUs = 0;
static int16_t buffers[3][512];
static std::atomic<bool> playing{true}, changeRequested{false}, repaintRequested{false};
static std::atomic<uint32_t> sceneInfo{0}, currentStep{0};
static std::atomic<uint8_t> hitAccum{0};
static std::atomic<uint32_t> worstRenderUs{0}, queueErrors{0};
static uint8_t volume = 165;
static const char* kitNames[] = {"Skin", "Box", "Brush", "Clay", "Glass", "Felt", "Wire"};
static const char* voiceNames[] = {"Kick", "Snare", "C-Hat", "O-Hat", "Wood", "Tom", "Rim"};
static const char* punchNames[] = {"", "Crush", "LoFi", "Dub Echo", "Feedback", "Oct Down"};

// Dev-only mode: front button plays and advances through one voice at a
// time, side button changes kit, screen names what's currently selected.
// Requests are handed to audioTask via atomics rather than calling into
// engine directly from here, the same pattern changeRequested already uses,
// since engine is not safe to touch from two tasks at once. Off by default
// so normal generative play (with punch effects) is what's on the device;
// flip to true for direct voice/kit A/B testing.
constexpr bool devMode = false;
static unsigned devVoiceIndex = 0, devKitIndex = 0;
static std::atomic<int> devTriggerRequest{-1}, devKitRequest{-1};

void audioTask(void*) {
  unsigned index = 0;
  for (;;) {
    if (changeRequested.exchange(false)) engine.newVariation();
    int devTrigger = devTriggerRequest.exchange(-1);
    if (devTrigger >= 0) engine.devTrigger(unsigned(devTrigger));
    int devKit = devKitRequest.exchange(-1);
    if (devKit >= 0) engine.setKitCharacter(unsigned(devKit));
    engine.setPlaying(playing.load());
    uint32_t start = micros();
    engine.render(buffers[index], 512);
    uint32_t elapsed = micros() - start;
    sceneInfo.store(engine.displayInfo());
    currentStep.store(engine.currentStep());
    hitAccum.fetch_or(engine.drainHits());
    if (elapsed > worstRenderUs) worstRenderUs = elapsed;
    while (!M5.Speaker.playRaw(buffers[index], 512, kit::rate, false, 1, 0)) {
      ++queueErrors;
      vTaskDelay(1);
    }
    index = (index + 1) % 3;
  }
}

void motionTask(void*) {
  for (;;) {
    if (M5.Imu.isEnabled() && (M5.Imu.update() & m5::IMU_Class::sensor_mask_accel)) {
      const auto data = M5.Imu.getImuData();
      if (shake.update(data.accel.x,data.accel.y,data.accel.z,millis())) repaintRequested = true;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void draw() {
  auto& d = M5.Display;
  d.fillScreen(0x1082);
  d.setTextColor(0xD692, 0x1082);
  d.setTextSize(3);
  d.setCursor(16, 14); d.print("DRUMS");
  d.drawFastHLine(16, 48, 208, 0x4208);
  d.setTextSize(2);
  uint32_t info = sceneInfo.load();
  static const char* activity[] = {"Sparse", "Steady", "Steady", "Full"};
  d.setCursor(16, 57); d.printf("%s %s", kitNames[(info >> 13) & 7], activity[(info >> 16) & 3]);
  d.setCursor(16, 82); d.printf("%02u %u BPM  bar %u", unsigned(info >> 21), unsigned(info & 127), unsigned((info >> 7) & 63));
  unsigned punch = (info >> 18) & 7;
  if (punch) { d.setCursor(16, 123); d.setTextSize(1); d.print(punchNames[punch]); d.setTextSize(2); }
  d.setCursor(16, 108);
  if (playing) d.printf("Vol %u%%", unsigned(volume) * 100 / 255);
  else d.print("resting");
  int battery = M5.Power.getBatteryLevel();
  d.setCursor(130,108);
  if (battery >= 0) d.printf("Bat %d%%", std::min(100,battery));
  else d.print("Bat --");
}

void drawDevMode() {
  auto& d = M5.Display;
  d.fillScreen(0x1082);
  d.setTextColor(0xD692, 0x1082);
  d.setTextSize(2);
  d.setCursor(16, 10); d.print("DEV MODE");
  d.drawFastHLine(16, 34, 208, 0x4208);
  d.setTextSize(3);
  d.setCursor(16, 48); d.print(kitNames[devKitIndex]);
  d.setTextSize(2);
  d.setCursor(16, 84); d.printf("Next: %s", voiceNames[devVoiceIndex]);
  d.setTextSize(1);
  d.setCursor(16, 112); d.print("Blue: play + next voice");
  d.setCursor(16, 122); d.print("Side: next kit");
}

// Temporary: pushes candidate color swatches through the exact same
// pack-into-uint16_t-then-pushImage path the real visuals use. Red/green
// quadrants confirmed the panel bleeds red and green into each other
// (pure red reads orange, pure green reads yellow) while blue and white
// were accurate; this sweep tests whether nudging red/green toward blue
// counteracts it. Remove once a palette is confirmed correct.
constexpr bool showColorTest = false;
void colorTest() {
  static uint16_t testFrame[240 * 135];
  auto pack = [](unsigned r, unsigned g, unsigned b) -> uint16_t {
    return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  };
  struct Swatch { unsigned r, g, b; };
  static const Swatch swatches[] = {
    {255, 0, 0},     // 1: pure red (baseline, reads orange)
    {255, 0, 60},    // 2: red nudged toward magenta
    {230, 0, 110},   // 3: red nudged further toward pink
    {0, 255, 0},     // 4: pure green (baseline, reads yellow)
    {0, 255, 120},   // 5: green nudged toward spring/teal
    {0, 220, 170},   // 6: green nudged further toward cyan
    {0, 90, 255},    // 7: blue (anchor, was accurate)
    {160, 0, 255},   // 8: purple (anchor)
  };
  constexpr unsigned n = sizeof(swatches) / sizeof(swatches[0]);
  unsigned swatchW = 240 / n;
  for (unsigned y = 0; y < 135; ++y) for (unsigned x = 0; x < 240; ++x) {
    unsigned idx = std::min(n - 1, x / swatchW);
    bool border = (x % swatchW) < 2;
    testFrame[y * 240 + x] = border ? 0 : pack(swatches[idx].r, swatches[idx].g, swatches[idx].b);
  }
  M5.Display.pushImage(0, 0, 240, 135, reinterpret_cast<const lgfx::rgb565_t*>(testFrame));
  for (;;) delay(1000); // Hold indefinitely; reflash with showColorTest=false to resume normal play.
}

void setup() {
  auto cfg = M5.config();
  cfg.internal_spk = true; cfg.internal_mic = false; cfg.internal_imu = true;
  M5.begin(cfg);
  Serial.begin(115200);
  engine.seed(esp_random());
  painting.seed(esp_random());
  sceneInfo.store(engine.displayInfo());
  M5.BtnA.setHoldThresh(650);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(55);
  M5.Speaker.setVolume(volume);
  if (showColorTest) colorTest();
  if (devMode) { engine.setSequencerMuted(true); engine.setKitCharacter(devKitIndex); }
  painting.render(0,0,0);
  M5.Display.pushImage(0,0,240,135,reinterpret_cast<const lgfx::rgb565_t*>(painting.pixels()));
  if (!M5.Speaker.begin()) {
    audioFailed = true; M5.Display.fillScreen(0x1082);
    M5.Display.setTextSize(2); M5.Display.setCursor(16, 62); M5.Display.print("audio error");
    return;
  }
  if (xTaskCreatePinnedToCore(motionTask, "drums-motion", 4096, nullptr, 1, nullptr, 0) != pdPASS)
    Serial.println("Motion task unavailable");
  if (xTaskCreatePinnedToCore(audioTask, "drums-audio", 4096, nullptr, 3, nullptr, 1) != pdPASS) {
    audioFailed = true; M5.Display.fillScreen(0x1082);
    M5.Display.setTextSize(2); M5.Display.setCursor(16, 62); M5.Display.print("audio error");
  }
}

void loop() {
  M5.update();
  if (devMode) {
    bool changed = false;
    if (M5.BtnA.wasClicked()) {
      devTriggerRequest.store(int(devVoiceIndex));
      devVoiceIndex = (devVoiceIndex + 1) % kit::voiceCount;
      changed = true;
    }
    if (M5.BtnB.wasClicked()) {
      devKitIndex = (devKitIndex + 1) % 7;
      devKitRequest.store(int(devKitIndex));
      changed = true;
    }
    static bool everDrawn = false;
    if (changed || !everDrawn) { drawDevMode(); everDrawn = true; }
    delay(10);
    return;
  }
  uint32_t now = millis();
  bool changed = false;
  if (M5.BtnA.wasClicked()) { changeRequested = true; playing = true; changed = true; }
  if (M5.BtnA.wasHold()) { playing = !playing; changed = true; }
  static uint32_t lastScene = 0;
  uint32_t currentScene = sceneInfo.load();
  const bool newGroove = lastScene != 0 && (currentScene >> 21) != (lastScene >> 21);
  if (currentScene != lastScene) { lastScene = currentScene; changed = true; }
  if (M5.BtnB.wasClicked()) {
    volume = volume >= 255 ? 45 : volume + 30;
    M5.Speaker.setVolume(volume); changed = true;
    infoVisible = true; infoAt = now;
  }
  static uint32_t frameAt = 0;
  const bool newVisual = repaintRequested.exchange(false);
  if (newGroove || newVisual) { painting.regenerate(); infoVisible=false; frameAt=now-83; }
  if (infoVisible && uint32_t(now - infoAt) >= 4000) infoVisible = false;
  static bool wasInfoVisible = false;
  static uint8_t lastHitBits = 0;
  static uint32_t lastHitAt = 0;
  if (!audioFailed) {
    if (infoVisible) {
      if (changed || !wasInfoVisible) draw();
    } else if (wasInfoVisible || uint32_t(now - frameAt) >= 83) {
      float dt = std::min(0.25f,float(uint32_t(now-frameAt))/1000);
      frameAt = now;
      uint32_t started = micros();
      uint8_t hits = hitAccum.exchange(0);
      if (hits) { lastHitBits = hits; lastHitAt = now; }
      painting.render(dt, hits, currentStep.load());
      M5.Display.pushImage(0,0,240,135,reinterpret_cast<const lgfx::rgb565_t*>(painting.pixels()));
      // Names whichever punch-in effect is currently active, for the whole
      // time it's engaged, so a bad-sounding one can be identified by eye
      // instead of guessing from timing.
      unsigned activePunch = (currentScene >> 18) & 7;
      if (activePunch) {
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(0xFFFF, 0x0000);
        M5.Display.setCursor(186, 2); // Fixed right-aligned-ish spot; "Oct Down" is the longest name.
        M5.Display.print(punchNames[activePunch]);
        M5.Display.setTextSize(2);
      }
      // Temporary diagnostic: names whichever voice(s) just fired, so a loud
      // hit can be identified by reading the screen instead of guessing from
      // visual position. Hidden for now; flip to true to bring it back.
      constexpr bool showHitDiagnostic = true;
      if (showHitDiagnostic && lastHitBits && uint32_t(now - lastHitAt) < 500) {
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(0xFFFF, 0x0000);
        M5.Display.setCursor(2, 2);
        bool first = true;
        for (unsigned v = 0; v < kit::voiceCount; ++v) if (lastHitBits & (1u << v)) {
          if (!first) M5.Display.print(' ');
          M5.Display.print(voiceNames[v]);
          first = false;
        }
        M5.Display.setTextSize(2);
      }
      worstVisualUs = std::max(worstVisualUs,uint32_t(micros()-started));
    }
  }
  wasInfoVisible = infoVisible;
  static uint32_t report = 0;
  if (millis() - report >= 10000) {
    report = millis();
    Serial.printf("render worst=%lu us / 16000 us; queue errors=%lu; heap=%u; generation=%lu BPM=%lu kit=%lu activity=%lu punch=%lu visual=%u visual_us=%lu\n",
      (unsigned long)worstRenderUs.load(), (unsigned long)queueErrors.load(), ESP.getFreeHeap(),
      (unsigned long)(currentScene >> 21), (unsigned long)(currentScene & 127),
      (unsigned long)((currentScene >> 13) & 7),(unsigned long)((currentScene >> 16) & 3),
      (unsigned long)((currentScene >> 18) & 7),
      painting.generation(),(unsigned long)worstVisualUs);
  }
  delay(10);
}
