# Project instructions for Rill Drums

## Never introduce synthetic noise-based drum synthesis

Snare, Closed Hat, Open Hat, Clap and Rim are one-shot samples (`src/Samples.h`), rendered offline via Tone.js's own instruments (`NoiseSynth` + `Filter`, `MembraneSynth`) and embedded as flash-resident PCM. Kick and Tom are the only voices still live-synthesized, as a pitch-swept sine, no noise involved.

Getting here took several rejected rounds: hand-tuned filtered-noise synthesis, both real-time on the device and an offline C++ renderer with more compute to spend, was tried repeatedly and always came back sounding cheap. The user's own words on the last attempt: "it's how you're mixing the noise in that sounds bad... you need a totally new process of making these." Do not go back to that well. Specifically:

- Do not add hand-rolled oscillator layers, extra filter stages, bitcrush, or other manual noise-shaping on top of a Tone.js instrument's own output. If a Tone.js instrument's stock output isn't distinct enough between kits, that's a signal to try a different stock instrument or parameter, not to start engineering the signal by hand again.
- Any change to how a voice sounds should go through the same pipeline: adjust parameters in the Tone.js render page in the scratch tooling, render offline, listen, then re-embed as samples. Never add DSP to the real-time path (`src/Kit.h`) for these five voices.
- If asked to add variety between kits (hats and claps have been flagged as too similar more than once), the fix is choosing genuinely different Tone.js instruments or their own built-in parameters, not manual signal-chain additions. Ask before trying anything that isn't a straightforward Tone.js instrument/parameter change.
