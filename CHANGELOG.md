# Changes

## Study 7

Two fixes and a musicality pass. The wireframe visuals (icosahedron, diamond, orbit) were quietly squashed to half height: the pitch rotation's `cos`/`sin` were each multiplied by an extra 0.5, which breaks the unit-circle identity a rotation matrix depends on, so every solid's vertical extent came out half its horizontal extent -- most visible as a "stretched wide" look against the 240x135 landscape screen. Removed the stray `0.5f`.

The new sample-based kits (Study 6) also read as too quiet -- traced to the 35 samples never being normalized against each other. Tone.js's pink noise (used by four of the seven kits) renders at a fraction of white noise's natural level, so raw peaks ranged from 11% to 89% across kits before this. Peak-normalized all 35 clips to a consistent 92% and regenerated `Samples.h`.

Fills were a fixed two-step tag (tom then rim) every time, same shape regardless of kit or bar. `scheduleFill()` now also picks a length (one to four steps) and a shape (a tom roll capped by a rim stinger, tom/rim alternating, or a snare run-up landing on tom/rim) each time a fill comes around, and velocity ramps up across the fill toward the downbeat instead of sitting flat. Swing no longer sits at one fixed amount for an entire variation: it now wanders lightly bar to bar as a fourth move in `mutateGroove()`, usually easing back toward straight time and only occasionally settling on a touch of shuffle, capped at 9% of a step so it reads as feel rather than sloppy timing.

## Study 6

Replaced procedural noise synthesis for Snare, Closed/Open Hat, Clap and Rim with pre-rendered one-shot samples. Every round of noise-filter tuning (real-time and offline, multiple noise colors, cascaded filters, bitcrush, procedural vinyl crackle) still read as "cheap" — the ceiling was the approach, not the parameters. Rendered 35 samples (5 voices x 7 kits) offline with Tone.js's actual synth engines (proper pink/white noise, tuned envelopes) via real offline rendering in a browser, then embedded the raw PCM (~828 KB) as flash-resident C arrays (`src/Samples.h`). Kick and Tom stay live-synthesized — pitch-swept sine already sounded right and didn't need replacing.

This deleted a large amount of code: the cascaded state-variable filters, hat metal-oscillator layer, clap crack/tail envelope, and Karplus-Strong pluck resonator are all gone, replaced by direct buffer playback. Render time dropped from ~6ms to ~2.8ms of the 16ms budget as a result. RAM usage is unchanged (samples are read directly from flash, never copied in); flash usage rose from 16.6% to 41.9%, still comfortable. Reset each voice's velocity balance to a neutral default since the old numbers compensated for the old synthesis engine's specific loudness quirks, which no longer apply.

## Study 5

The Study 4 hat/clap variety fix didn't actually reach the device before the next flash was tried — but even correctly built, its hat frequency range (4200-10200 Hz) sat in the same too-high register that made the original hats indistinguishable, so it wouldn't have fixed the complaint anyway. Moved both the noise filter's center frequency and the new metal partials' fundamental down into the 1000-4400 Hz range already confirmed audible on this speaker (from earlier snare/clap balance work), and widened decay-time spread per kit (closed-hat decay now 0.02-0.10s, was 0.03-0.07s) since duration differences are robust regardless of speaker response. Also caught and fixed a real bug the register rewrite introduced: two of the seven kit rows (Glass, Wire) had their snare fields dropped mid-edit, silently zero-filling later fields — added a test that isolates each kit character and checks it's audible on its own, which would have caught this before it ever reached a build.

Punch-in effect names now show on screen for the entire time an effect is engaged, not just when the volume/data view happens to be open — added after a report that one of them "doesn't sound good."

Added a dev-only test mode (`devMode` in `main.cpp`, off by default): front button plays and advances through one voice at a time, side button changes kit character directly, screen names both. Requests are routed through the same atomic-flag pattern the generative groove already uses to talk to the audio task, since `Engine` isn't safe to call into from two tasks at once.

## Study 4

Two new visual families: a circular/radial version of the step grid (concentric rings per voice, radar-style playhead sweep) and a rotating wireframe icosahedron whose edges flash by voice on hit. Fixed the Rings family to draw hollow circles instead of filled discs, punching a true background-colored hole rather than the earlier broken shade-0 attempt. Shake now cycles through four families instead of two.

Replaced the muted pastel color palettes with four bolder, more saturated ones. Enlarged the icosahedron (the favorite so far). Generalized its rotation/projection code into a reusable `Solid` so two more wireframe families could share it cheaply: a rotating wireframe diamond (octahedron) and Orbit, a seven-node constellation — one node per voice, positioned on a sphere by the golden-angle method and connected to its two nearest neighbors, each node flashing on its own voice's hit. Six visual families total.

The first bolder palette still read as "ugly shades" — the in-between hues (teal, lime, muddy purple) weren't actually simpler, just louder. Tried four near-primary palettes with each channel close to fully on or off (pure red/green/blue, cyan/magenta/yellow); that read as "Windows and Linux" rather than the requested "Apple, more pure." Tried Apple's own iOS/macOS system colors next; still called "ugly."

At that point stopped guessing and tested the actual display: pushed pure red/green/blue/white quadrants through the real pixel pipeline. Blue and white were accurate; red read a little orange and green read very yellow in isolation. A follow-up sweep tried nudging red and green toward blue to compensate — but side by side, the *pure* red and green actually read as the truest colors of the set; the nudged ones were worse, not better. Final palette: pure red, pure green, and the two hues confirmed clean in the sweep (a blue and a purple), plus pink and cyan chosen from the same clean territory (only one of red/green present alongside blue). Added a temporary on-screen color-swatch test (`showColorTest` in `main.cpp`, off by default) used to run these comparisons; left in place in case another round is needed.

## Study 4

Closed hat, open hat and clap were all just filtered noise at a different center frequency and decay — a narrow sonic space that made kits sound similar, especially on those three voices. Added a three-partial inharmonic "metal" oscillator layer under the hats (per-kit mix and spread control how much ring vs. plain noise, and how far the partials spread from a unison tone), so kits now range from brushed/noisy (Brush, Skin, Felt) to genuinely ringing and metallic (Glass, Wire). Claps gained a per-kit pulse count (2-4, was hardcoded to 3) for tighter or looser flam character. Fixed a real bug this exposed: `Channel::pulseAt` was sized for exactly 3 pulses.

## Study 2

First flash to a physical StickS3. Trimmed the snare's velocity; it sat far louder than every other voice through the built-in speaker despite unremarkable raw amplitude, most likely the speaker's midrange sensitivity rather than a synthesis bug. Added Pocket Operator/EP-133-style punch-in effects that semi-randomly punch in on the master mix for about a bar and clear themselves automatically.

Replaced the initial Lowpass/Hipass Sweep pair with a tempo-synced Dub Echo (feedback delay, tape-style darkening on repeats, pitch wobble). Gave every punch effect internal motion instead of one flat setting for its whole window: Stutter now accelerates through three shrinking loop lengths, Bit Crush and Lo-Fi wobble/step their quantization, Feedback swells in and back out, Octave Down glides the pitch down and back rather than snapping. Six effects total.

Made the dub delay always-on at a modest level instead of only appearing as the Dub Echo punch; the punch now throws the same line's feedback, mix, wobble and tap length up temporarily. Added a temporary on-screen diagnostic (names whichever voice just fired) to track down a voice that still reads too loud after the snare trim — turned out to be the clap, not the snare.

Fixed the actual bug: the clap's flam re-triggered its noise envelope to a flat 0.9 on the second and third pulses regardless of note velocity, so trimming its level could never fully tame it. Scaled that floor by the note's own velocity instead, then reset the clap's base velocity to 0.55 now that it actually responds. Toned the always-on dub delay down further so it sits under the mix rather than announcing itself.

## Initial repository — Study 1

First scaffold: seven synthesized percussion voices, seven kit characters, a generative sixteen-step groove with humanization, swing, mutation and fills, and two hit-reactive visual families (rings and step grid). Host tests cover audio stability, seed reproducibility, the open/closed hat exclusion rule, and shake detection. Not yet run on physical hardware.
