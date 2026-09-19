// Copyright (c) 2026 Bruce Blay
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Six procedural visual families driven by drum hits rather than continuous
// audio level. Shared by firmware and the host preview tool.
namespace pulse {
class Painting {
 public:
  static constexpr unsigned width = 240, height = 135;
  static constexpr unsigned voices = 7, steps = 16;
  static constexpr unsigned families = 6;

 private:
  struct Color { float r, g, b; };
  struct Vec3 { float x, y, z; };
  // A small rotating wireframe: up to 12 vertices, edges found once by
  // connecting every pair at the shortest pairwise distance rather than by
  // hand-transcribing a table.
  struct Solid {
    std::array<Vec3, 12> base{};
    std::array<std::array<uint8_t, 2>, 30> edges{};
    unsigned vertexCount = 0, edgeCount = 0;
  };
  std::array<uint16_t, width * height> frame{};
  uint32_t rng;
  unsigned kind = 0, palette = 0, count = 0;
  float phase = 0, breath = 0;
  Color ink[3]{};
  Color backgroundColor{12, 14, 24};
  uint16_t background = 0;

  struct Ripple { bool active = false; float x = 0, y = 0, radius = 0, life = 0; unsigned voice = 0; };
  std::array<Ripple, 16> ripples{};
  unsigned nextRipple = 0;

  std::array<std::array<float, steps>, voices> cellGlow{};
  unsigned playhead = 0;

  Solid icosa, diamond, orbit;
  std::array<float, 30> edgeGlow{};
  std::array<float, voices> nodeGlow{};
  float solidYaw = 0, solidPitch = 0;

  unsigned random() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
  float unit() { return float(random() >> 8) / 16777216.0f; }
  static uint16_t color(Color c, float shade = 1) {
    unsigned r = unsigned(std::max(0.0f, std::min(255.0f, c.r * shade)));
    unsigned g = unsigned(std::max(0.0f, std::min(255.0f, c.g * shade)));
    unsigned b = unsigned(std::max(0.0f, std::min(255.0f, c.b * shade)));
    return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  }
  void span(int y, int left, int right, uint16_t c) {
    if (y < 0 || y >= int(height)) return;
    left = std::max(0, left); right = std::min(int(width) - 1, right);
    for (int x = left; x <= right; ++x) frame[unsigned(y) * width + unsigned(x)] = c;
  }
  void disc(float cx, float cy, float r, Color c, float shade = 1) {
    if (r < 0.5f) return;
    int top = std::max(0, int(std::floor(cy - r))), bottom = std::min(int(height) - 1, int(std::ceil(cy + r)));
    for (int y = top; y <= bottom; ++y) {
      float v = (y + 0.5f - cy) / r;
      if (std::abs(v) > 1) continue;
      float extent = r * std::sqrt(1 - v * v);
      span(y, int(std::ceil(cx - extent)), int(std::floor(cx + extent)), color(c, shade));
    }
  }
  // A hollow ring: the outer disc with a same-shape hole punched back to the
  // real background color, not just darkened.
  void ring(float cx, float cy, float r, float thickness, Color c, float shade = 1) {
    disc(cx, cy, r, c, shade);
    if (r > thickness) disc(cx, cy, r - thickness, backgroundColor, 1);
  }
  void line(float x, float y, float ex, float ey, Color c, float shade = 1) {
    int n = int(std::max(std::abs(ex - x), std::abs(ey - y))) + 1;
    uint16_t col = color(c, shade);
    for (int i = 0; i <= n; ++i) {
      float t = float(i) / n;
      int px = int(x + (ex - x) * t), py = int(y + (ey - y) * t);
      if (px >= 0 && px < int(width) && py >= 0 && py < int(height)) frame[unsigned(py) * width + unsigned(px)] = col;
    }
  }
  void circleGuide(float cx, float cy, float r, Color c, float shade) {
    constexpr unsigned segments = 28;
    float px = cx + r, py = cy;
    for (unsigned i = 1; i <= segments; ++i) {
      float a = float(i) * (2 * 3.14159265f / segments);
      float x = cx + std::cos(a) * r, y = cy + std::sin(a) * r;
      line(px, py, x, y, c, shade);
      px = x; py = y;
    }
  }
  void anchor(unsigned voice, float& x, float& y) const {
    float angle = float(voice) * (2 * 3.14159265f / voices) - 1.5707963f;
    x = width / 2.0f + std::cos(angle) * 62;
    y = height / 2.0f + std::sin(angle) * 42;
  }
  void spawnRipple(unsigned voice) {
    Ripple& r = ripples[nextRipple];
    nextRipple = (nextRipple + 1) % ripples.size();
    r.active = true; r.life = 1; r.radius = 3;
    anchor(voice, r.x, r.y);
    r.voice = voice;
  }
  static Solid buildSolid(const Vec3* raw, unsigned n) {
    Solid s; s.vertexCount = n;
    for (unsigned i = 0; i < n; ++i) {
      float m = std::sqrt(raw[i].x * raw[i].x + raw[i].y * raw[i].y + raw[i].z * raw[i].z);
      s.base[i] = {raw[i].x / m, raw[i].y / m, raw[i].z / m};
    }
    float minDist = 1e9f;
    for (unsigned i = 0; i < n; ++i) for (unsigned j = i + 1; j < n; ++j) {
      float dx = s.base[i].x - s.base[j].x, dy = s.base[i].y - s.base[j].y, dz = s.base[i].z - s.base[j].z;
      minDist = std::min(minDist, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    s.edgeCount = 0;
    for (unsigned i = 0; i < n && s.edgeCount < s.edges.size(); ++i)
      for (unsigned j = i + 1; j < n && s.edgeCount < s.edges.size(); ++j) {
        float dx = s.base[i].x - s.base[j].x, dy = s.base[i].y - s.base[j].y, dz = s.base[i].z - s.base[j].z;
        if (std::sqrt(dx * dx + dy * dy + dz * dz) < minDist * 1.01f) s.edges[s.edgeCount++] = {uint8_t(i), uint8_t(j)};
      }
    return s;
  }
  void buildSolids() {
    float phi = 1.6180339887f;
    Vec3 icosaRaw[12] = {
      {0, 1, phi}, {0, 1, -phi}, {0, -1, phi}, {0, -1, -phi},
      {1, phi, 0}, {1, -phi, 0}, {-1, phi, 0}, {-1, -phi, 0},
      {phi, 0, 1}, {phi, 0, -1}, {-phi, 0, 1}, {-phi, 0, -1}};
    icosa = buildSolid(icosaRaw, 12);
    Vec3 diamondRaw[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    diamond = buildSolid(diamondRaw, 6);
    // Seven nodes, one per voice, spread over a sphere by the golden-angle
    // method; each node connects to its two nearest neighbors.
    float goldenAngle = 3.14159265f * (3 - std::sqrt(5.0f));
    Vec3 orbitRaw[voices];
    for (unsigned v = 0; v < voices; ++v) {
      float y = 1 - (float(v) / (voices - 1)) * 2;
      float r = std::sqrt(std::max(0.0f, 1 - y * y));
      float theta = goldenAngle * v;
      orbitRaw[v] = {std::cos(theta) * r, y, std::sin(theta) * r};
    }
    orbit.vertexCount = voices; orbit.edgeCount = 0;
    orbit.base = {};
    for (unsigned v = 0; v < voices; ++v) orbit.base[v] = orbitRaw[v];
    bool connected[voices][voices] = {};
    for (unsigned v = 0; v < voices; ++v) {
      float d0 = 1e9f, d1 = 1e9f; unsigned n0 = v, n1 = v;
      for (unsigned u = 0; u < voices; ++u) if (u != v) {
        float dx = orbitRaw[v].x - orbitRaw[u].x, dy = orbitRaw[v].y - orbitRaw[u].y, dz = orbitRaw[v].z - orbitRaw[u].z;
        float d = dx * dx + dy * dy + dz * dz;
        if (d < d0) { d1 = d0; n1 = n0; d0 = d; n0 = u; } else if (d < d1) { d1 = d; n1 = u; }
      }
      for (unsigned n : {n0, n1}) {
        unsigned a = std::min(v, n), b = std::max(v, n);
        if (a != b && !connected[a][b] && orbit.edgeCount < orbit.edges.size()) {
          connected[a][b] = true; orbit.edges[orbit.edgeCount++] = {uint8_t(a), uint8_t(b)};
        }
      }
    }
  }
  void renderRings(float dt) {
    // A slow shared pulse gives the resting screen a living, breathing feel.
    float heartbeat = 0.5f + 0.5f * std::sin(phase * 1.6f);
    disc(width / 2.0f, height / 2.0f, 10 + heartbeat * 3, ink[2], 0.20f + breath * 0.22f);
    static const float growth[voices] = {30, 22, 11, 16, 18, 24, 10};
    static const float speed[voices] = {1.4f, 1.9f, 3.4f, 2.6f, 2.4f, 1.7f, 3.6f};
    for (auto& r : ripples) {
      if (!r.active) continue;
      r.life -= dt * speed[r.voice];
      if (r.life <= 0) { r.active = false; continue; }
      r.radius = 5 + (1 - r.life) * growth[r.voice];
      float shade = std::min(1.0f, r.life * 1.3f);
      ring(r.x, r.y, r.radius, std::max(1.5f, r.radius * 0.22f), ink[r.voice % 3], shade);
    }
  }
  void renderGrid(float dt) {
    float cellW = float(width) / steps, cellH = 15;
    float top = (height - cellH * voices) / 2;
    for (unsigned v = 0; v < voices; ++v) {
      for (unsigned s = 0; s < steps; ++s) {
        cellGlow[v][s] *= std::exp(-dt * 5.0f);
        float x = s * cellW, y = top + v * cellH;
        float base = 0.10f + 0.05f * ((s % 4) == 0);
        float shade = base + cellGlow[v][s] * 0.9f;
        disc(x + cellW / 2, y + cellH / 2, std::min(cellW, cellH) * 0.36f, ink[v % 3], shade);
      }
    }
    float x = playhead * cellW;
    for (int y = 0; y < int(height); ++y) span(y, int(x), int(x + 1), color(ink[2], 0.35f));
  }
  void renderRadialGrid(float dt) {
    float cx = width / 2.0f, cy = height / 2.0f;
    float innerR = 13, ringGap = 7.2f, maxR = innerR + ringGap * (voices - 1) + 4;
    for (unsigned v = 0; v < voices; ++v) {
      float r = innerR + v * ringGap;
      circleGuide(cx, cy, r, ink[v % 3], 0.07f);
      for (unsigned s = 0; s < steps; ++s) {
        cellGlow[v][s] *= std::exp(-dt * 5.0f);
        float a = float(s) * (2 * 3.14159265f / steps) - 1.5707963f;
        float x = cx + std::cos(a) * r, y = cy + std::sin(a) * r;
        float shade = 0.10f + cellGlow[v][s] * 0.9f;
        disc(x, y, 3.1f, ink[v % 3], shade);
      }
    }
    // A radar-style sweep pointing at the current step.
    float a = float(playhead) * (2 * 3.14159265f / steps) - 1.5707963f;
    line(cx, cy, cx + std::cos(a) * maxR, cy + std::sin(a) * maxR, ink[2], 0.4f);
  }
  void project(const Solid& s, float size, std::array<float, 12>& px, std::array<float, 12>& py, std::array<float, 12>& pz) {
    float cosY = std::cos(solidYaw), sinY = std::sin(solidYaw);
    // solidPitch's own 0.6f keeps the tip gentle; cos/sin themselves must stay
    // a true unit rotation, or x and y end up scaled unevenly (previously
    // halved cos/sin here made every solid squashed vertically -- stretched
    // wide against the 240x135 landscape screen).
    float cosP = std::cos(solidPitch * 0.6f), sinP = std::sin(solidPitch * 0.6f);
    float cx = width / 2.0f, cy = height / 2.0f;
    for (unsigned i = 0; i < s.vertexCount; ++i) {
      float x = s.base[i].x, y = s.base[i].y, z = s.base[i].z;
      float x1 = x * cosY - z * sinY, z1 = x * sinY + z * cosY;
      float y2 = y * cosP - z1 * sinP, z2 = y * sinP + z1 * cosP;
      // A gentle perspective divide, not a physically exact camera.
      float scale = 2.6f / (2.6f + z2);
      px[i] = cx + x1 * size * scale;
      py[i] = cy + y2 * size * scale;
      pz[i] = z2;
    }
  }
  void renderSolid(float dt, const Solid& s, float size) {
    std::array<float, 12> px{}, py{}, pz{};
    project(s, size, px, py, pz);
    for (unsigned e = 0; e < s.edgeCount; ++e) {
      edgeGlow[e] *= std::exp(-dt * 3.0f);
      unsigned a = s.edges[e][0], b = s.edges[e][1];
      float depth = 1 - (pz[a] + pz[b]) * 0.22f; // nearer edges read a little brighter
      float shade = std::max(0.14f, std::min(1.0f, 0.42f * depth + edgeGlow[e] * 0.85f));
      line(px[a], py[a], px[b], py[b], ink[e % 3], shade);
    }
  }
  void renderOrbit(float dt) {
    std::array<float, 12> px{}, py{}, pz{};
    project(orbit, 50 + breath * 6, px, py, pz);
    for (unsigned e = 0; e < orbit.edgeCount; ++e) {
      unsigned a = orbit.edges[e][0], b = orbit.edges[e][1];
      float depth = 1 - (pz[a] + pz[b]) * 0.22f;
      line(px[a], py[a], px[b], py[b], ink[e % 3], std::max(0.08f, 0.22f * depth));
    }
    for (unsigned v = 0; v < voices; ++v) {
      nodeGlow[v] *= std::exp(-dt * 4.0f);
      float depth = 1 - pz[v] * 0.3f;
      float r = 3 + nodeGlow[v] * 6;
      float shade = std::max(0.25f, std::min(1.0f, 0.45f * depth + nodeGlow[v] * 0.7f));
      disc(px[v], py[v], r, ink[v % 3], shade);
    }
    (void)dt;
  }

 public:
  explicit Painting(uint32_t value = 17) : rng(value ? value : 1) { buildSolids(); regenerate(); }
  void seed(uint32_t value) { rng = value ? value : 1; count = 0; regenerate(); }
  void regenerate() {
    // Grid and Radial Grid hidden from rotation for now (not deleted, just
    // excluded from the cycle); flip this back to `(kind+1)%families` /
    // `random()%families` to bring them back.
    static const unsigned activeKinds[] = {0, 3, 4, 5};
    constexpr unsigned activeCount = 4;
    unsigned activeIndex = 0;
    for (unsigned i = 0; i < activeCount; ++i) if (activeKinds[i] == kind) activeIndex = i;
    kind = count ? activeKinds[(activeIndex + 1) % activeCount] : activeKinds[random() % activeCount];
    palette = count ? (palette + 1 + random() % 3) % 4 : random() % 4;
    ++count; phase = unit() * 6.283185f; breath = 0;
    // On-device swatch tests settled this: nudging red/green toward other
    // hues to "correct" them looked worse, not better — pure red, pure
    // green, this blue and this purple were the ones confirmed to read
    // true on the actual panel. Pink and cyan sit in the same clean
    // territory (only one of red/green present alongside blue).
    static const Color palettes[4][3] = {
      {{255, 0, 0}, {0, 255, 0}, {0, 90, 255}},
      {{160, 0, 255}, {255, 20, 147}, {0, 90, 255}},
      {{255, 0, 0}, {160, 0, 255}, {0, 220, 255}},
      {{0, 255, 0}, {0, 90, 255}, {255, 20, 147}}};
    for (unsigned i = 0; i < 3; ++i) ink[i] = palettes[palette][i];
    background = color(backgroundColor);
    for (auto& r : ripples) r.active = false;
    for (auto& row : cellGlow) row.fill(0);
    edgeGlow.fill(0); nodeGlow.fill(0);
    solidYaw = unit() * 6.283185f; solidPitch = unit() * 6.283185f;
    frame.fill(background);
  }
  unsigned generation() const { return count; }
  unsigned visualFamily() const { return kind; }
  const uint16_t* pixels() const { return frame.data(); }
  void render(float dt, uint8_t hitMask, unsigned currentStep) {
    dt = std::max(0.0f, std::min(0.1f, dt));
    phase += dt; if (phase > 628.3185f) phase -= 628.3185f;
    breath += (float(hitMask != 0) - breath) * std::min(1.0f, dt * 4);
    solidYaw += dt * 0.55f; if (solidYaw > 6.28319f) solidYaw -= 6.28319f;
    solidPitch += dt * 0.21f; if (solidPitch > 6.28319f) solidPitch -= 6.28319f;
    playhead = currentStep % steps;
    for (unsigned v = 0; v < voices; ++v) {
      if (!(hitMask & (1u << v))) continue;
      if (kind == 0) spawnRipple(v);
      else if (kind == 1 || kind == 2) cellGlow[v][playhead] = 1;
      else if (kind == 5) nodeGlow[v] = 1;
      else { const Solid& s = kind == 3 ? icosa : diamond; for (unsigned e = 0; e < s.edgeCount; ++e) if (e % voices == v) edgeGlow[e] = 1; }
    }
    frame.fill(background);
    if (kind == 0) renderRings(dt);
    else if (kind == 1) renderGrid(dt);
    else if (kind == 2) renderRadialGrid(dt);
    else if (kind == 3) renderSolid(dt, icosa, 62 + breath * 8);
    else if (kind == 4) renderSolid(dt, diamond, 50 + breath * 8);
    else renderOrbit(dt);
  }
};
}
