# DubStarMooniCon

A two-oscillator subharmonic voice for Ableton Move, as a
[Schwung](https://github.com/charlesvestal/schwung) sound generator.

**Status: working, 1.0.** Builds and runs on a Move. The off-device contract
test passes. The arpeggiator was removed before release — see *Removed features*
below, because knowing why is more useful than a changelog entry.

---

## What it is

Two oscillators, each with two subharmonic taps:

```
                 ┌───────── core (÷1) ─────────┐
   note pitch ──►│                             ├──► mixer ──┐
                 ├──── tap 1 (÷N) ─────────────┤            │
                 └──── tap 2 (÷M) ─────────────┘            │
                                                            ├──► filter ──► VCA ──► out
                 ┌───────── core (÷1) ─────────┐            │
   note pitch ──►│                             ├──► mixer ──┘
                 ├──── tap 1 (÷N) ─────────────┤
                 └──── tap 2 (÷M) ─────────────┘
```

**VCO 1 is polyphonic** (4 voices, each with its own oscillator and divide
chain). **VCO 2 is monophonic.** The taps read `phase × div`, so they stay
phase-locked to the core — which is what makes a divider chain sound like one
instrument rather than three oscillators that happen to be in tune.

## Pages

| Page | Knobs 1–8 |
|---|---|
| **root** | Cutoff · Resonance · VCO 1 · Sub 1 · Sub 2 · VCO 2 · Decay · Volume |
| **Osc 1** | Wave · Sub 1 Freq · Sub 2 Freq · Octave · VCO 1 Level · Sub 1 Level · Sub 2 Level |
| **Osc 2** | the same eight, monophonic |
| **Filter** | Cutoff · Resonance · Mode · Env Amount · Attack · Decay · VCA Attack · VCA Decay |
| **Tune** | VCO 1 Octave · VCO 2 Octave · Volume · Cutoff |

Root declares its own knobs: root is where the instrument is played from, not a
menu.

## Parameters — 25

Every pitch control is a **stepped selector**: an enum behind a float parameter
with `step: 1`. There is no Hz anywhere in the UI and nothing to tune by ear on
a device with no display.

| Control | Values |
|---|---|
| **Wave** (per VCO) | Pulse · Pulse 25 · Saw · Tri · Sine |
| **Sub Freq** (four of them) | 1:1 · 1:2 · 1:3 · 1:4 · 1:6 · 1:8 · 1:9 · 1:12 |
| **Octave** (per VCO) | −5 … +5, whole octaves |
| **Filter Mode** | Lowpass · Bandpass · Highpass |

The divisor table is **pure 3-limit only** — every entry keeps `1/div` a
`2^a · 3^b` ratio, so every subharmonic the instrument produces is an octave, a
fifth, or a stack of the two. The impure divisors (5, 7, 10, 11, 13, 14, 15)
were considered and left out; eight entries is also an easier stepped selector
than sixteen.

## Two things in the audio path that look wrong and are not

### The gain law

```c
const double voice_gain = 1.0 / 6.0;   /* 2 VCOs x 3 sources */
```

Three sources can sum inside one VCO and two VCOs can sum into the filter, so
six signals can overlap. Scaling by 1/6 is a **bound, not a tuned value**: it
holds at every setting rather than for one patch.

Without it the summed input reaches 1.4 (two VCOs at the default 0.7 each), the
SVF's integrator states run 40% past their intended range and clip *inside the
filter* — and because that happens before the volume multiply, the instrument
distorts at **any** volume. That was a real fault in 0.1: the sound broke up
even with the volume low, and turning the knob down never helped. The old
`* 0.35` output gain was hiding the problem rather than fixing it.

### The filter's stability ceiling

```c
double g = tan(M_PI * fc / sr);
if (g > 0.5) g = 0.5;                  /* stability ceiling */
```

A trapezoidal state variable filter is stable only while `g < 1`. Above that
the two integrator states grow every sample instead of decaying, and the output
becomes full-scale noise.

The cutoff knob maps to 30 Hz … 15.4 kHz. At the top of that range `g` reaches
**1.94** — so a frequency clamp alone does not protect anything:

| Cutoff knob | Frequency | `g` | |
|---|---|---|---|
| 0.75 | 3,229 Hz | 0.234 | fine |
| 0.90 | 8,231 Hz | 0.664 | marginal |
| 0.95 | 11,244 Hz | **1.03** | unstable |
| 1.00 | 15,360 Hz | **1.94** | unstable |

**The limit is on the coefficient, not the frequency.** An earlier version
clamped `fc` instead, which never fired because the clamp was set at
`sr * 0.45` (19,845 Hz) — a frequency the knob cannot reach. Opening Cutoff
produced very loud noise as a result.

The ceiling is applied in **two** places, and both are needed: to the base `g`
in `refresh_coeffs`, and to `gmod` *after* the cutoff envelope multiplies it by
up to 5×. Clamping only the base leaves `g × 5` running past the limit.

## No allocation, no library calls, no file I/O

Every plugin entry point runs on the SPI audio callback — SCHED_FIFO 70, pinned
to core 3, roughly 2,370 µs of slack per 128-frame block. So:

- **No allocation** after `create_instance`.
- **No file I/O.** There is no sample loader and no sample data; the instrument
  has nothing to read.
- **No `pow`, `sqrt`, `sin`, `exp`, `tan`, `fmod` or `floor` in the render
  loop.** Every value that changes only on a *parameter change* is precomputed:
  the octave ratio and divisor scales per voice, the envelope and filter
  coefficients per instance.

That last point was a real fault too. The first version called `pow(2, oct)`
and `1/sqrt(div)` **inside** the per-sample, per-voice loop — roughly 176,000
library calls a second for values that change when a knob turns. That is what
made the instrument click under load. If you add a control, compute its
coefficient where the parameter is **set**, never in `render`.

The band-limited shapes are the one exception, and only where they must be:
PolyBLEP needs two divisions per *discontinuity*, so the saw has one per sample
and the pulses two. Sine and triangle have none.

## Removed features

**The arpeggiator.** It was written, tested off-device, and removed before
release. The clock was correct (`arp_inc` computed properly, `g = 0.000181` at
1/16 and 120 bpm) but the note never advanced: six hundred rendered blocks
produced exactly one pitch — the last note played — so `arp_advance` ran and
its result never reached the oscillator. After several failed attempts to
isolate why, it was cut rather than shipped broken. A control that does nothing
is worse than no control.

**The internal sequencers and clock.** An earlier version carried a two-
sequencer polyrhythm engine. It was removed for a specific reason worth
recording: the step knobs resolved through a degree-to-ratio table that
**nothing in the render path ever called**, so turning them did nothing
audible. Move already has a sequencer, and it is a better one.

**Sample playback.** A built-in sine was once readable as a core oscillator,
with the taps dividing its playback rate. It was removed; the taps are now
purely phase dividers on an oscillator.

**The 3-limit "Lattice" quantise mode.** A scale whose ratios were all
`2^a · 3^b`. Its table was correct and its purity was tested, but it went with
the sequencers — with nothing generating pitch from scale degrees, a quantise
mode has nothing to quantise.

## Files

```
src/suborg.h                    engine API and types; no host types
src/suborg.c                    the engine: oscillators, taps, filter, render
src/suborg_meta.h               chain_params and ui_hierarchy, as JSON strings
src/schwung_dubstarmoonicon.c   the plugin wrapper
src/host_sim.c                  off-device contract test
module.json                     manifest, 501 bytes
vendor/schwung/plugin_api_v1.h  Schwung's header, vendored
scripts/{build,install,test}.sh build, deploy, verify
```

## Build and test

Needs only a C compiler. No Schwung checkout, no Docker, no device:

```bash
bash scripts/test.sh
```

That compiles the engine plus the plugin wrapper and runs nine sections of
checks: the divisor table, plugin lifecycle, the JSON services, string-valued
`set_param`, interleaved int16 output, all five waveforms, band-limiting
(measured, not asserted), the filter modes, and a walk over all 25 parameters.

To build for the Move and deploy:

```bash
bash scripts/build.sh                 # cross-compile dsp.so in Docker
DEVICE=ableton@move.local bash scripts/install.sh
```

## Platform notes

- **`plugin_api_v2_t` is closed and the wrapper does not restate it.** It
  includes the host's header, uses designated initializers (so a missing field
  stays NULL rather than shifting every pointer after it), and carries a
  `_Static_assert` on the field order. Appending to that struct from a module
  is what boot-looped a device via header drift.
- **`create_instance` does one `calloc`, starts a worker, and returns.** The
  worker demotes itself off SCHED_FIFO 70 first (threads inherit the priority,
  and a FIFO 70 worker would starve Move's own Link Main at FIFO 35), pins to
  cores 0–2 to leave core 3 for SPI, builds, and publishes with a release
  store. Building inline in `create_instance` is what produced
  `took 4294967.295 ms on the SPI callback` — the `0xFFFFFFFF` µs overflow
  sentinel — which stalls the whole device rather than just the slot.
- **The worker's argument is heap-allocated.** Two earlier versions passed the
  sample rate either via a field the worker could read before the store landed,
  or via a **stack** struct whose frame was destroyed when `create_instance`
  returned. Both failed identically: the worker read zeros, `build_tables`
  rejected the rate at its `< 8000.0` guard, and `ready` never published.
- **A sound generator's `ui_hierarchy` is read only from the plugin.** A
  hierarchy declared in `module.json` is silently ignored.

## Licensing and credits

Original code. The instruments below are credited as influences; none of their
source is copied.

| Influence | What was learned from it | Licence |
|---|---|---|
| **Moog Subharmonicon** | The panel vocabulary: stepped selectors, subharmonic dividers, the insight that the instrument needs no tuning UI at all | — |
| [Model-72](https://github.com/sd88me/schwung-model72) | The Schwung module layout, deferred init, the measured LO/HI costs, `module.json` under the loader cap, root declaring its own knobs | GPL-3.0-or-later |
| [setBfree for Schwung](https://github.com/pdubucq/schwung-setbfree) | The failure deferred init exists to avoid, and hierarchy-from-the-plugin for a sound generator | GPL-2.0 |
| Stylophone CPM DS-2 / DF-8 | Sub-oscillators, hard sync, self-oscillating filters, multimode filter vocabulary | — |

Schwung's plugin API header is MIT, © Charles Vestal and contributors, and is
vendored verbatim in `vendor/schwung/`.
