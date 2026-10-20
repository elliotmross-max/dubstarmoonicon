/* suborg.h -- DubStarMooniCon, a two-oscillator subharmonic voice for Ableton
 * Move / Schwung.
 *
 * WHAT THIS IS
 *   Two oscillators, each with two subharmonic taps. VCO 1 is polyphonic
 *   (4 voices, each with its own oscillator and divide chain). VCO 2 is
 *   monophonic. One shared resonant filter follows the mix, with a cutoff
 *   envelope that tracks the amplitude envelope's shape.
 *
 * THE VOICE LIFETIME -- read this before touching note_off
 *   A voice is NOT killed by note_off. note_off drops the gate and starts the
 *   release; the voice stays active while the envelope falls, and is retired
 *   only when the envelope reaches IDLE. The version this replaces set
 *   active = false immediately, which silenced the oscillator before the
 *   release could be heard -- the envelope ran correctly into a voice the
 *   render loop had already skipped, so every note ended dead.
 *
 * THE ENVELOPE
 *   Full ADSR, twice: the amplitude and the filter cutoff share one set of
 *   coefficients and one gate, so loudness and cutoff move on one contour.
 *   Times are chosen from a named table (see suborg_env_time_t), so the knob
 *   reads as a musical time rather than an opaque 0..1.
 *
 * EVERY PITCH CONTROL IS A NAMED STEPPED SELECTOR
 *   Octaves, divisors, filter modes, waveform and every envelope time are
 *   enums with labels, so the display tells you what you selected.
 *
 * PERFORMANCE
 *   No pow/sqrt/sin/exp/floor/fmod in the audio loop. Everything that changes
 *   only on a parameter change is cached.
 */

#ifndef SUBORG_H
#define SUBORG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBORG_VOICES        4
#define SUBORG_MAX_BLOCK     128

/* ---------------------------------------------------------------------------
 * Divisors -- the tap ratios, with their interval names
 * ------------------------------------------------------------------------ */

typedef struct {
    uint8_t div;          /* the divisor applied to the core              */
    const char *name;     /* shown on the display                          */
} suborg_divisor_t;

extern const suborg_divisor_t suborg_divisors[8];
#define SUBORG_DIVISOR_COUNT 8

/* ---------------------------------------------------------------------------
 * Waveforms
 * ------------------------------------------------------------------------ */

typedef enum {
    SUBORG_WAVE_PULSE = 0,
    SUBORG_WAVE_PULSE25,
    SUBORG_WAVE_SAW,
    SUBORG_WAVE_TRI,
    SUBORG_WAVE_SINE,
    SUBORG_WAVE_COUNT
} suborg_wave_t;

extern const char *suborg_wave_names[SUBORG_WAVE_COUNT];

/* ---------------------------------------------------------------------------
 * Filter modes
 * ------------------------------------------------------------------------ */

typedef enum {
    SUBORG_LP = 0,
    SUBORG_BP,
    SUBORG_HP,
    SUBORG_FILTER_MODE_COUNT
} suborg_filter_mode_t;

extern const char *suborg_filter_mode_names[SUBORG_FILTER_MODE_COUNT];

/* ---------------------------------------------------------------------------
 * Octave offsets -- named, instead of a bare integer
 *
 * Seven positions: two octaves down to two up. The old control was an int from
 * -5 to +5, which showed as a number and gave no clue which way was down.
 * ------------------------------------------------------------------------ */

typedef enum {
    SUBORG_OCT_M2 = 0,    /* -2 Oct */
    SUBORG_OCT_M1,        /* -1 Oct */
    SUBORG_OCT_UNISON,    /* Unison */
    SUBORG_OCT_P1,        /* +1 Oct */
    SUBORG_OCT_P2,        /* +2 Oct */
    SUBORG_OCT_COUNT
} suborg_octave_t;

extern const int         suborg_octave_semitones[SUBORG_OCT_COUNT];
extern const char *const suborg_octave_names[SUBORG_OCT_COUNT];

/* ---------------------------------------------------------------------------
 * Envelope times -- a named table
 *
 * The knob is an index into a table of real times, so the display says "80 ms"
 * or "1 s" instead of 0.37. Attack and the decay/release tables differ: an
 * attack rarely needs four seconds, and a release rarely needs one millisecond.
 * ------------------------------------------------------------------------ */

typedef enum {
    SUBORG_ATK_1MS = 0, SUBORG_ATK_5MS, SUBORG_ATK_20MS, SUBORG_ATK_80MS,
    SUBORG_ATK_300MS, SUBORG_ATK_1S, SUBORG_ATK_3S,
    SUBORG_ATK_COUNT
} suborg_attack_time_t;

typedef enum {
    SUBORG_DEC_5MS = 0, SUBORG_DEC_20MS, SUBORG_DEC_80MS, SUBORG_DEC_300MS,
    SUBORG_DEC_1S, SUBORG_DEC_2S, SUBORG_DEC_4S,
    SUBORG_DEC_COUNT
} suborg_decay_time_t;

extern const double suborg_attack_seconds[SUBORG_ATK_COUNT];
extern const double suborg_decay_seconds[SUBORG_DEC_COUNT];
extern const char  *const suborg_attack_names[SUBORG_ATK_COUNT];
extern const char  *const suborg_decay_names[SUBORG_DEC_COUNT];

/* ---------------------------------------------------------------------------
 * Filter envelope amount -- named steps
 * ------------------------------------------------------------------------ */

typedef enum {
    SUBORG_FENV_OFF = 0,
    SUBORG_FENV_25,
    SUBORG_FENV_50,
    SUBORG_FENV_75,
    SUBORG_FENV_100,
    SUBORG_FENV_COUNT
} suborg_filt_env_t;

extern const float  suborg_filt_env_amounts[SUBORG_FENV_COUNT];
extern const char  *const suborg_filt_env_names[SUBORG_FENV_COUNT];

/* ---------------------------------------------------------------------------
 * Envelope stages
 * ------------------------------------------------------------------------ */

typedef enum {
    SUBORG_ENV_IDLE = 0,
    SUBORG_ENV_ATTACK,
    SUBORG_ENV_DECAY,
    SUBORG_ENV_SUSTAIN,
    SUBORG_ENV_RELEASE
} suborg_env_stage_t;

extern const char *const suborg_env_stage_names[5];

typedef struct {
    double  level;
    double  sustain;
    uint8_t stage;
} suborg_env_t;

/* ---------------------------------------------------------------------------
 * Voice
 *
 * `releasing` marks a voice whose key is up but whose envelope is still
 * falling. Such a voice keeps rendering -- that is the whole point.
 * ------------------------------------------------------------------------ */

typedef struct {
    double  phase;
    double  inc;
    double  freq;
    double  oct_ratio;
    double  scale1, scale2;
    uint8_t div1, div2;
    double  lvl_osc, lvl_sub1, lvl_sub2;
    uint8_t wave;
    bool    active;
    bool    releasing;
    int     note;
    uint32_t age;
    unsigned dirty;
} suborg_voice_t;

/* ---------------------------------------------------------------------------
 * Coefficients
 * ------------------------------------------------------------------------ */

typedef struct {
    double att, dec, rel;
    double filt_g;
    double filt_damp;
} suborg_coeff_t;

/* ---------------------------------------------------------------------------
 * Instance
 * ------------------------------------------------------------------------ */

typedef struct {
    double  sample_rate;
    double  base_note;
    int32_t ready;

    /* --- parameters ------------------------------------------------------ */
    uint8_t wave[2];
    uint8_t div_sel[2][2];
    uint8_t oct_sel[2];       /* suborg_octave_t                        */
    float   lvl[2][3];
    float   cutoff;
    float   resonance;
    uint8_t filt_env_sel;     /* suborg_filt_env_t                      */
    uint8_t filt_mode;
    uint8_t atk_sel;          /* suborg_attack_time_t                   */
    uint8_t dec_sel;          /* suborg_decay_time_t                    */
    uint8_t rel_sel;          /* suborg_decay_time_t                    */
    float   env_sustain;
    float   volume;

    /* --- aftertouch ------------------------------------------------------ */
    float   pressure;         /* 0..1, from poly or channel aftertouch   */

    suborg_coeff_t coeff;

    /* --- state ----------------------------------------------------------- */
    suborg_voice_t  voice[SUBORG_VOICES];
    int     held[8];
    int     held_count;
    uint32_t note_age;

    suborg_env_t env_amp;
    suborg_env_t env_filt;

    int     gate;
    double  svf_ic1, svf_ic2;
} suborg_t;

/* ---------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------ */

int   suborg_build_tables(suborg_t *s, double sample_rate, double base_hz);
void  suborg_render(suborg_t *s, float *out_l, float *out_r, int frames);
void  suborg_note_on(suborg_t *s, int note, float vel);
void  suborg_note_off(suborg_t *s, int note);
void  suborg_set_pressure(suborg_t *s, float pressure);

double suborg_env_step(suborg_env_t *e, const suborg_coeff_t *c);

int   suborg_set_param(suborg_t *s, const char *key, float value);
float suborg_get_param(suborg_t *s, const char *key);

const char *suborg_ui_hierarchy(void);

#ifdef __cplusplus
}
#endif

#endif /* SUBORG_H */
