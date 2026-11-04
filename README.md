# DubStarMooniCon

A two-oscillator subharmonic voice for Ableton Move, as a
[Schwung](https://github.com/charlesvestal/schwung) sound generator.

**Status: working.** Builds for the Move, installs from the module tarball, and
plays on hardware.

---

## What it is

Two oscillators, each with two subharmonic taps that divide its pitch:

```
                 ┌───────── core (÷1) ─────────┐
   note pitch ──►│                             ├──► mixer ──┐
                 ├──── tap 1 (÷N) ─────────────┤            │
                 └──── tap 2 (÷M) ─────────────┘            ├──► filter ──► VCA ──► out
                                                            │
                 ┌───────── core (÷1) ─────────┐            │
   note pitch ──►│                             ├──► mixer ──┘
                 ├──── tap 1 (÷N) ─────────────┤
                 └──── tap 2 (÷M) ─────────────┘
```

**The first oscillator is polyphonic** — four voices, each with its own
oscillator and divide chain, so chords ring. **The second is monophonic**,
which keeps the low end solid under whatever the first is playing.

The taps read `phase × div`, so they stay phase-locked to the core. That is
what makes a divider chain sound like one instrument rather than three
oscillators that happen to be in tune.

**There is no tuning.** Every pitch control is a stepped selector — waveform,
octave, and the two tap divisions — so there is nothing to set by ear on a
device with no keyboard display.

## Installing on Move

Install from the Module Store in Schwung, or from the release tarball:

```bash
scp dubstarmoonicon-module.tar.gz ableton@move.local:/data/UserData/
ssh ableton@move.local 'tar -xzf /data/UserData/dubstarmoonicon-module.tar.gz \
  -C /data/UserData/schwung/modules/sound_generators/'
```

The tarball's top level is `dubstarmoonicon/`. Extract it into the folder
**above** the module folder, not into the module folder itself.

Reboot the Move, then add DubStarMooniCon to a Signal Chain slot.

## First five minutes

1. Play pads. The first oscillator spreads across four voices; the second sits
   underneath, monophonic.
2. Turn **Sub 1 Level** up on the Osc 1 page to hear the divided tap a fifth
   below the core.
3. Set the two **Sub Freq** selectors on Osc 1 to different divisions — 1:3 and
   1:2, say — and the tap pair becomes a chord against the core.
4. The **Filter Attack** and **Decay** all start at 0, so the filter is still.
   Raise Env Amount on the Filter page and the cutoff moves with each note.

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
with `step: 1`. There is no Hz anywhere in the UI.

Subharmonic taps use 3-limit ratios only — 1:2, 1:3, 1:4, 1:6, 1:8, 1:9, 1:12 —
so every division stays in tune with the core and with the other tap.

## Files

```
src/suborg.h                    engine API and types; no host types
src/suborg.c                    the engine: oscillators, taps, filter, render
src/suborg_meta.h               chain_params and ui_hierarchy, as JSON strings
src/schwung_dubstarmoonicon.c   the plugin wrapper
src/host_sim.c                  off-device contract test
module.json                     the manifest
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

## Design notes

### `plugin_api_v2_t` is closed and the wrapper does not restate it

It includes the host's header, uses designated initializers (so a missing field
stays NULL rather than shifting every pointer after it), and carries a
`_Static_assert` on the field order. Appending to that struct from a module is
what boot-looped a device via header drift.

### `create_instance` does one `calloc`, starts a worker, and returns

The worker demotes itself off `SCHED_FIFO 70` first — threads inherit the
priority, and a FIFO 70 worker would starve Move's own Link Main at FIFO 35 —
pins to cores 0–2 to leave core 3 for the audio callback, builds its tables,
and publishes with a release store.

Building inline in `create_instance` is what produced
`took 4294967.295 ms on the SPI callback` — the `0xFFFFFFFF` µs overflow
sentinel — which stalls the whole device rather than just the slot.

### The worker's argument is heap-allocated

Two earlier versions passed the sample rate either via a field the worker could
read before the store landed, or via a **stack** struct whose frame was
destroyed when `create_instance` returned. Both failed identically: the worker
read zeros, `build_tables` rejected the rate at its `< 8000.0` guard, and
`ready` never published.

### A sound generator's `ui_hierarchy` is read only from the plugin

A hierarchy declared in `module.json` is silently ignored.

### The package is flat

`dsp.so`, `module.json` and `help.json` sit at the tarball root and extract
straight into the module folder. Wrapping them in a `dubstarmoonicon/`
directory and extracting into the parent nests them one level too deep and the
module never loads.

## Removed features

**Polyrhythmic step sequencers.** Removed before release, not for lack of
function but because the instrument reads better without them: the voice is the
instrument, and the sequencers made it a different one.

**A lattice quantise mode.** Its table was correct and its purity was tested,
but it went with the sequencers — with nothing generating pitch from scale
degrees, a quantise mode has nothing to quantise.

## Credits and licensing

Original code, MIT. No third-party source is copied. Schwung's plugin API header
is MIT, © Charles Vestal and contributors, and is vendored verbatim in
`vendor/schwung/`.

Thanks to [Schwung](https://github.com/charlesvestal/schwung) for the module
platform and its documentation, and to
[Model-72](https://github.com/sd88me/schwung-model72) and
[setBfree for Schwung](https://github.com/pdubucq/schwung-setbfree) for the
module layout conventions followed here.
