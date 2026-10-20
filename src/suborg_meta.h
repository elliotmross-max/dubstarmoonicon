/* suborg_meta.h -- DubStarMooniCon: chain_params and ui_hierarchy.
 *
 * WRITTEN WHOLE. Patching this file by pattern has failed every time, because
 * the JSON lives inside C string concatenations where one misplaced character
 * breaks the whole translation unit.
 *
 * Every control that selects a discrete thing is an ENUM with named options,
 * so the display says what you picked rather than showing a number:
 *
 *   wave          Pulse / Pulse 25 / Saw / Triangle / Sine
 *   sub ratio     1:2 Octave / 1:3 Fifth / 1:6 5th+Oct / ...
 *   octave        -2 Octave / -1 Octave / Unison / +1 Octave / +2 Octave
 *   filter mode   Lowpass / Bandpass / Highpass
 *   env amount    Off / 25% / 50% / 75% / 100%
 *   attack        1 ms ... 3 s
 *   decay/release 5 ms ... 4 s
 *
 * Continuous knobs are only the ones where a sweep is genuinely wanted:
 * Cutoff, Resonance, Sustain, Volume, and AT>Release.
 */

#ifndef SUBORG_META_H
#define SUBORG_META_H

static const char *SUBORG_CHAIN_PARAMS =
"["
  /* --- oscillators ------------------------------------------------------- */
  "{\"key\":\"wave1\",\"name\":\"VCO 1 Wave\",\"type\":\"enum\",\"options\":[\"Pulse\",\"Pulse 25\",\"Saw\",\"Triangle\",\"Sine\"],\"default\":0},"
  "{\"key\":\"wave2\",\"name\":\"VCO 2 Wave\",\"type\":\"enum\",\"options\":[\"Pulse\",\"Pulse 25\",\"Saw\",\"Triangle\",\"Sine\"],\"default\":0},"
  "{\"key\":\"oct1\",\"name\":\"VCO 1 Octave\",\"type\":\"enum\",\"options\":[\"-2 Octave\",\"-1 Octave\",\"Unison\",\"+1 Octave\",\"+2 Octave\"],\"default\":2},"
  "{\"key\":\"oct2\",\"name\":\"VCO 2 Octave\",\"type\":\"enum\",\"options\":[\"-2 Octave\",\"-1 Octave\",\"Unison\",\"+1 Octave\",\"+2 Octave\"],\"default\":2},"
  /* --- subharmonic taps -------------------------------------------------- */
  "{\"key\":\"div1a\",\"name\":\"VCO 1 Sub 1\",\"type\":\"enum\",\"options\":[\"1:1 Uni\",\"1:2 Octave\",\"1:3 Fifth\",\"1:4 2 Octave\",\"1:6 5th+Oct\",\"1:8 3 Octave\",\"1:9 2 Fifths\",\"1:12 5th+2 Oct\"],\"default\":2},"
  "{\"key\":\"div1b\",\"name\":\"VCO 1 Sub 2\",\"type\":\"enum\",\"options\":[\"1:1 Uni\",\"1:2 Octave\",\"1:3 Fifth\",\"1:4 2 Octave\",\"1:6 5th+Oct\",\"1:8 3 Octave\",\"1:9 2 Fifths\",\"1:12 5th+2 Oct\"],\"default\":0},"
  "{\"key\":\"div2a\",\"name\":\"VCO 2 Sub 1\",\"type\":\"enum\",\"options\":[\"1:1 Uni\",\"1:2 Octave\",\"1:3 Fifth\",\"1:4 2 Octave\",\"1:6 5th+Oct\",\"1:8 3 Octave\",\"1:9 2 Fifths\",\"1:12 5th+2 Oct\"],\"default\":2},"
  "{\"key\":\"div2b\",\"name\":\"VCO 2 Sub 2\",\"type\":\"enum\",\"options\":[\"1:1 Uni\",\"1:2 Octave\",\"1:3 Fifth\",\"1:4 2 Octave\",\"1:6 5th+Oct\",\"1:8 3 Octave\",\"1:9 2 Fifths\",\"1:12 5th+2 Oct\"],\"default\":0},"
  /* --- levels ------------------------------------------------------------ */
  "{\"key\":\"lvl1_osc\",\"name\":\"VCO 1 Level\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.7},"
  "{\"key\":\"lvl1_sub1\",\"name\":\"VCO 1 Sub 1 Level\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"lvl1_sub2\",\"name\":\"VCO 1 Sub 2 Level\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"lvl2_osc\",\"name\":\"VCO 2 Level\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.7},"
  "{\"key\":\"lvl2_sub1\",\"name\":\"VCO 2 Sub 1 Level\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"lvl2_sub2\",\"name\":\"VCO 2 Sub 2 Level\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  /* --- envelope (ADSR, shared by amplitude and filter cutoff) ------------ */
  "{\"key\":\"env_attack\",\"name\":\"Attack\",\"type\":\"enum\",\"options\":[\"1 ms\",\"5 ms\",\"20 ms\",\"80 ms\",\"300 ms\",\"1 s\",\"3 s\"],\"default\":2},"
  "{\"key\":\"env_decay\",\"name\":\"Decay\",\"type\":\"enum\",\"options\":[\"5 ms\",\"20 ms\",\"80 ms\",\"300 ms\",\"1 s\",\"2 s\",\"4 s\"],\"default\":3},"
  "{\"key\":\"env_sustain\",\"name\":\"Sustain\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.70},"
  "{\"key\":\"env_release\",\"name\":\"Release\",\"type\":\"enum\",\"options\":[\"5 ms\",\"20 ms\",\"80 ms\",\"300 ms\",\"1 s\",\"2 s\",\"4 s\"],\"default\":3},"
  /* --- filter ------------------------------------------------------------ */
  "{\"key\":\"cutoff\",\"name\":\"Cutoff\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.75},"
  "{\"key\":\"resonance\",\"name\":\"Resonance\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.25},"
  "{\"key\":\"filt_mode\",\"name\":\"Filter Mode\",\"type\":\"enum\",\"options\":[\"Lowpass\",\"Bandpass\",\"Highpass\"],\"default\":0},"
  "{\"key\":\"filt_env\",\"name\":\"Env Amount\",\"type\":\"enum\",\"options\":[\"Off\",\"25%\",\"50%\",\"75%\",\"100%\"],\"default\":0},"
  /* --- output ------------------------------------------------------------ */
  "{\"key\":\"volume\",\"name\":\"Volume\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.8},"
  /* --- read-only status --------------------------------------------------- */
  "{\"key\":\"ready\",\"name\":\"Ready\",\"type\":\"int\",\"min\":0,\"max\":1,\"readonly\":true},"
  "{\"key\":\"voices\",\"name\":\"Voices\",\"type\":\"int\",\"min\":0,\"max\":4,\"readonly\":true},"
  "{\"key\":\"env_stage\",\"name\":\"Env Stage\",\"type\":\"enum\",\"options\":[\"Idle\",\"Attack\",\"Decay\",\"Sustain\",\"Release\"],\"readonly\":true},"
  "{\"key\":\"env_level\",\"name\":\"Env Level\",\"type\":\"float\",\"min\":0,\"max\":1,\"readonly\":true},"
  "{\"key\":\"pressure\",\"name\":\"Pressure\",\"type\":\"float\",\"min\":0,\"max\":1,\"readonly\":true}"
"]";

/* --------------------------------------------------------------------------
 * ui_hierarchy -- five pages. Root declares its own knobs: root is where the
 * instrument is played from, not a menu.
 * ------------------------------------------------------------------------ */

static const char *SUBORG_UI_HIERARCHY =
"{\"modes\":null,\"levels\":{"
  "\"root\":{"
    "\"label\":\"DubStarMooniCon\","
    "\"knobs\":[\"cutoff\",\"resonance\",\"lvl1_osc\",\"lvl1_sub1\",\"lvl1_sub2\",\"lvl2_osc\",\"env_sustain\",\"volume\"],"
    "\"params\":["
      "{\"key\":\"cutoff\",\"label\":\"Cutoff\"},"
      "{\"key\":\"resonance\",\"label\":\"Resonance\"},"
      "{\"key\":\"lvl1_osc\",\"label\":\"VCO 1\"},"
      "{\"key\":\"lvl1_sub1\",\"label\":\"Sub 1\"},"
      "{\"key\":\"lvl1_sub2\",\"label\":\"Sub 2\"},"
      "{\"key\":\"lvl2_osc\",\"label\":\"VCO 2\"},"
      "{\"key\":\"env_sustain\",\"label\":\"Sustain\"},"
      "{\"key\":\"volume\",\"label\":\"Volume\"},"
      "{\"level\":\"osc1\",\"label\":\"Osc 1\"},"
      "{\"level\":\"osc2\",\"label\":\"Osc 2\"},"
      "{\"level\":\"env\",\"label\":\"Envelope\"},"
      "{\"level\":\"filter\",\"label\":\"Filter\"},"
      "{\"level\":\"tune\",\"label\":\"Tune\"}"
    "]},"
  "\"osc1\":{\"label\":\"Osc 1\","
    "\"knobs\":[\"wave1\",\"div1a\",\"div1b\",\"oct1\",\"lvl1_osc\",\"lvl1_sub1\",\"lvl1_sub2\"],"
    "\"params\":["
      "{\"key\":\"wave1\",\"label\":\"Wave\"},"
      "{\"key\":\"div1a\",\"label\":\"Sub 1 Ratio\"},"
      "{\"key\":\"div1b\",\"label\":\"Sub 2 Ratio\"},"
      "{\"key\":\"oct1\",\"label\":\"Octave\"},"
      "{\"key\":\"lvl1_osc\",\"label\":\"VCO 1 Level\"},"
      "{\"key\":\"lvl1_sub1\",\"label\":\"Sub 1 Level\"},"
      "{\"key\":\"lvl1_sub2\",\"label\":\"Sub 2 Level\"}"
    "]},"
  "\"osc2\":{\"label\":\"Osc 2\","
    "\"knobs\":[\"wave2\",\"div2a\",\"div2b\",\"oct2\",\"lvl2_osc\",\"lvl2_sub1\",\"lvl2_sub2\"],"
    "\"params\":["
      "{\"key\":\"wave2\",\"label\":\"Wave\"},"
      "{\"key\":\"div2a\",\"label\":\"Sub 1 Ratio\"},"
      "{\"key\":\"div2b\",\"label\":\"Sub 2 Ratio\"},"
      "{\"key\":\"oct2\",\"label\":\"Octave\"},"
      "{\"key\":\"lvl2_osc\",\"label\":\"VCO 2 Level\"},"
      "{\"key\":\"lvl2_sub1\",\"label\":\"Sub 1 Level\"},"
      "{\"key\":\"lvl2_sub2\",\"label\":\"Sub 2 Level\"}"
    "]},"
  "\"env\":{\"label\":\"Envelope\","
    "\"params\":["
      "{\"key\":\"env_attack\",\"label\":\"Attack\"},"
      "{\"key\":\"env_decay\",\"label\":\"Decay\"},"
      "{\"key\":\"env_sustain\",\"label\":\"Sustain\"},"
      "{\"key\":\"env_release\",\"label\":\"Release\"},"
      "{\"key\":\"env_level\",\"label\":\"Level\"},"
      "{\"key\":\"env_stage\",\"label\":\"Stage\"}"
    "]},"
  "\"filter\":{\"label\":\"Filter\","
    "\"knobs\":[\"cutoff\",\"resonance\",\"filt_mode\",\"filt_env\",\"env_attack\",\"env_decay\",\"env_sustain\",\"env_release\"],"
    "\"params\":["
      "{\"key\":\"cutoff\",\"label\":\"Cutoff\"},"
      "{\"key\":\"resonance\",\"label\":\"Resonance\"},"
      "{\"key\":\"filt_mode\",\"label\":\"Mode\"},"
      "{\"key\":\"filt_env\",\"label\":\"Env Amount\"},"
      "{\"key\":\"env_attack\",\"label\":\"Attack\"},"
      "{\"key\":\"env_decay\",\"label\":\"Decay\"},"
      "{\"key\":\"env_sustain\",\"label\":\"Sustain\"},"
      "{\"key\":\"env_release\",\"label\":\"Release\"}"
    "]},"
  "\"tune\":{\"label\":\"Tune\","
    "\"knobs\":[\"oct1\",\"oct2\",\"volume\",\"cutoff\"],"
    "\"params\":["
      "{\"key\":\"oct1\",\"label\":\"VCO 1 Octave\"},"
      "{\"key\":\"oct2\",\"label\":\"VCO 2 Octave\"},"
      "{\"key\":\"pressure\",\"label\":\"Pressure\"},"
      "{\"key\":\"volume\",\"label\":\"Volume\"}"
    "]}}}";

#endif /* SUBORG_META_H */
