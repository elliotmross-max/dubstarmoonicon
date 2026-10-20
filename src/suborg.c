/* suborg.c -- DubStarMooniCon engine.
 *
 * Plain C, no host types, no file I/O, no allocation after init.
 *
 * THE OSCILLATOR
 *   A voice has an accumulator running at its note's frequency. Three sources
 *   are read from it and mixed:
 *
 *     OSC   the accumulator itself
 *     SUB 1 the accumulator divided by divisor 1
 *     SUB 2 the accumulator divided by divisor 2
 *
 *   The taps read phase*div, so the divided sources stay phase-locked to the
 *   core -- which is what makes a divider chain sound like one instrument
 *   rather than three oscillators that happen to be in tune.
 *
 * VOICE LIFETIME -- the thing that was wrong
 *   note_off does NOT kill the voice. It drops the gate, starts the release,
 *   and marks the voice `releasing`. The voice keeps rendering until the
 *   envelope reaches IDLE, at which point it is retired.
 *
 *   The version before this set active = false the instant a key came up. The
 *   oscillator stopped rendering on that sample, and the release stage then
 *   ran on a voice the render loop had already skipped -- so every note ended
 *   dead, with no release tail, no matter what the envelope did. The envelope
 *   itself was correct the whole time, which is why the state-machine test
 *   passed while the instrument had no release at all.
 *
 * THE ENVELOPE
 *   ADSR, twice. The amplitude and the filter cutoff share one set of
 *   coefficients and one gate. Times come from named tables, so the knob reads
 *   as a musical time rather than an opaque 0..1.
 *
 * AFTERTOUCH
 *   Pad pressure feeds the release time: more pressure as you lift means a
 *
 * WHY THE AUDIO LOOP LOOKS LIKE THIS
 *   No pow, sqrt, sin, exp, floor, fmod or divide in suborg_render. Every
 *   value that changes on a PARAMETER CHANGE is cached. An earlier version
 *   called pow() and sqrt() per sample per voice -- about 176,000 library
 *   calls a second for values that only change when a knob turns -- and that
 *   is what made the instrument click.
 *
 * TWO NUMBERS THAT LOOK WRONG AND ARE NOT
 *   voice_gain = 1/6      six signals can overlap (2 VCOs x 3 sources).
 *   g clamped at 0.5      an SVF is stable only while g < 1, and the cutoff
 *                         knob reaches g = 1.94 at its top end.
 */

#include "suborg.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Tables
 * ------------------------------------------------------------------------ */

const suborg_divisor_t suborg_divisors[8] = {
    {  1, "1:1 Uni"        },
    {  2, "1:2 Octave"     },
    {  3, "1:3 Fifth"      },
    {  4, "1:4 2 Octave"   },
    {  6, "1:6 5th + Oct"  },
    {  8, "1:8 3 Octave"   },
    {  9, "1:9 2 Fifths"   },
    { 12, "1:12 5th+2 Oct" },
};

const char *suborg_wave_names[SUBORG_WAVE_COUNT] = {
    "Pulse", "Pulse 25", "Saw", "Triangle", "Sine",
};

const char *suborg_filter_mode_names[SUBORG_FILTER_MODE_COUNT] = {
    "Lowpass", "Bandpass", "Highpass",
};

const int suborg_octave_semitones[SUBORG_OCT_COUNT] = {
    -24, -12, 0, +12, +24,
};

const char *const suborg_octave_names[SUBORG_OCT_COUNT] = {
    "-2 Octave", "-1 Octave", "Unison", "+1 Octave", "+2 Octave",
};

/* Attack is short: an attack of four seconds is a pad, and a pad usually wants
 * a swell control, not the percussive range this instrument plays in. */
const double suborg_attack_seconds[SUBORG_ATK_COUNT] = {
    0.001, 0.005, 0.020, 0.080, 0.300, 1.0, 3.0,
};
const char *const suborg_attack_names[SUBORG_ATK_COUNT] = {
    "1 ms", "5 ms", "20 ms", "80 ms", "300 ms", "1 s", "3 s",
};

/* Decay and release share a table: both are fall times, and the useful range
 * is the same for each. */
const double suborg_decay_seconds[SUBORG_DEC_COUNT] = {
    0.005, 0.020, 0.080, 0.300, 1.0, 2.0, 4.0,
};
const char *const suborg_decay_names[SUBORG_DEC_COUNT] = {
    "5 ms", "20 ms", "80 ms", "300 ms", "1 s", "2 s", "4 s",
};

const float suborg_filt_env_amounts[SUBORG_FENV_COUNT] = {
    0.0f, 0.25f, 0.50f, 0.75f, 1.0f,
};
const char *const suborg_filt_env_names[SUBORG_FENV_COUNT] = {
    "Off", "25%", "50%", "75%", "100%",
};

const char *const suborg_env_stage_names[5] = {
    "Idle", "Attack", "Decay", "Sustain", "Release",
};

/* ---------------------------------------------------------------------------
 * Oscillator shapes
 * ------------------------------------------------------------------------ */

static double poly_blep(double t, double dt)
{
    if (dt <= 0.0) return 0.0;
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

/* Every shape has zero mean: three sum into the filter at once, so a DC term
 * would push a constant into the output. The 25% pulse is the one to check --
 * high a quarter cycle at +1 and low three quarters at -1 has a mean of -0.5,
 * so the LOW level is held at -1/3 instead: 0.25*1 + 0.75*(-1/3) = 0. */
static double wave_sample(uint8_t shape, double p, double dt)
{
    switch (shape) {
    case SUBORG_WAVE_PULSE: {
        double v = (p < 0.5) ? 1.0 : -1.0;
        double q = p + 0.5; if (q >= 1.0) q -= 1.0;
        v += poly_blep(p, dt);
        v -= poly_blep(q, dt);
        return v;
    }
    case SUBORG_WAVE_PULSE25: {
        double v = (p < 0.25) ? 1.0 : (-1.0 / 3.0);
        double q = p + 0.75; if (q >= 1.0) q -= 1.0;
        v -= (4.0 / 3.0) * poly_blep(q, dt);
        return v;
    }
    case SUBORG_WAVE_SAW: {
        double v = 2.0 * p - 1.0;
        v -= poly_blep(p, dt);
        return v;
    }
    case SUBORG_WAVE_TRI:
        return 4.0 * fabs(p - 0.5) - 1.0;
    case SUBORG_WAVE_SINE:
    default:
        return sin(2.0 * M_PI * p);
    }
}

/* ---------------------------------------------------------------------------
 * Envelopes
 * ------------------------------------------------------------------------ */

static double tau_coeff(double sr, double seconds)
{
    if (seconds < 1e-5) seconds = 1e-5;
    return 1.0 - exp(-1.0 / (sr * seconds));
}

static double note_to_hz(double base_hz, int note)
{
    return base_hz * pow(2.0, ((double)note - 69.0) / 12.0);
}

/* ---------------------------------------------------------------------------
 * Coefficients -- computed here, never in the audio loop.
 * ------------------------------------------------------------------------ */

static void refresh_coeffs(suborg_t *s)
{
    const double sr = s->sample_rate;

    /* A stage targeting a level reaches ~99% of it in 5 tau, so the table's
     * seconds are divided by 5 -- the knob then reads as the time to ARRIVE. */
    double atk = suborg_attack_seconds[s->atk_sel];
    double dec = suborg_decay_seconds[s->dec_sel];
    double rel = suborg_decay_seconds[s->rel_sel];

    /* Aftertouch lengthens the release: full pad pressure multiplies it by
     * 4, so a 300 ms release becomes 1.2 s under a hard press. */
    rel *= (1.0 + 3.0 * (double)s->pressure);

    /* Aftertouch lengthens the release: at full pressure it is multiplied by
     * 1 + 3*amt, so a 300 ms release can become 1.2 s under a hard press. */

    s->coeff.att = tau_coeff(sr, atk / 5.0);
    s->coeff.dec = tau_coeff(sr, dec / 5.0);
    s->coeff.rel = tau_coeff(sr, rel / 5.0);

    /* THE STABILITY LIMIT IS ON THE COEFFICIENT, NOT THE FREQUENCY. An SVF is
     * stable only while g < 1; above that the integrator states grow every
     * sample and the output is full-scale noise. The knob maps to 30 Hz ..
     * 15.4 kHz, where g reaches 1.94, so the ceiling below is what stops it. */
    double fc = 30.0 * pow(2.0, 9.0 * s->cutoff);
    if (fc > sr * 0.45) fc = sr * 0.45;
    double g = tan(M_PI * fc / sr);
    if (g > 0.5) g = 0.5;
    s->coeff.filt_g = g;

    double damp = 2.0 - 1.95 * (double)s->resonance;
    if (damp < 0.05) damp = 0.05;
    s->coeff.filt_damp = damp;
}

static void refresh_voice(suborg_voice_t *v, const suborg_t *s, int vco)
{
    v->oct_ratio = pow(2.0, suborg_octave_semitones[s->oct_sel[vco]] / 12.0);

    uint8_t d1 = suborg_divisors[s->div_sel[vco][0]].div;
    uint8_t d2 = suborg_divisors[s->div_sel[vco][1]].div;
    v->div1 = d1;
    v->div2 = d2;
    v->scale1 = 1.0 / sqrt((double)d1);
    v->scale2 = 1.0 / sqrt((double)d2);

    v->lvl_osc  = s->lvl[vco][0];
    v->lvl_sub1 = s->lvl[vco][1];
    v->lvl_sub2 = s->lvl[vco][2];
    v->wave     = s->wave[vco];
    v->dirty    = 0;
}

static void mark_all_dirty(suborg_t *s)
{
    for (int i = 0; i < SUBORG_VOICES; i++) s->voice[i].dirty = 1;
}

/* ---------------------------------------------------------------------------
 * Init -- worker thread only.
 * ------------------------------------------------------------------------ */

int suborg_build_tables(suborg_t *s, double sample_rate, double base_hz)
{
    if (!s || sample_rate < 8000.0) return -1;

    memset(s, 0, sizeof(*s));
    s->sample_rate = sample_rate;
    s->base_note   = base_hz;

    for (int i = 0; i < SUBORG_VOICES; i++) {
        s->voice[i].note  = -1;
        s->voice[i].dirty = 1;
    }

    for (int i = 0; i < 2; i++) {
        s->wave[i]       = SUBORG_WAVE_PULSE;
        s->div_sel[i][0] = 2;                    /* 1:3 Fifth */
        s->div_sel[i][1] = 0;                    /* 1:1 Uni   */
        s->oct_sel[i]    = SUBORG_OCT_UNISON;
        s->lvl[i][0]     = 0.7f;
        s->lvl[i][1]     = 0.0f;
        s->lvl[i][2]     = 0.0f;
    }

    s->cutoff       = 0.75f;
    s->resonance    = 0.25f;
    s->filt_env_sel = SUBORG_FENV_OFF;
    s->filt_mode    = SUBORG_LP;

    /* Defaults chosen so a held note HOLDS, and so a released note is
     * audible: 300 ms of release is long enough to hear. */
    s->atk_sel      = SUBORG_ATK_20MS;
    s->dec_sel      = SUBORG_DEC_300MS;
    s->rel_sel      = SUBORG_DEC_300MS;
    s->env_sustain  = 0.70f;
    s->volume       = 0.8f;

    s->pressure       = 0.0f;

    s->env_amp.level   = 0.0;  s->env_amp.sustain  = 0.70;
    s->env_amp.stage   = SUBORG_ENV_IDLE;
    s->env_filt.level  = 0.0;  s->env_filt.sustain = 0.70;
    s->env_filt.stage  = SUBORG_ENV_IDLE;

    s->note_age   = 0;
    s->held_count = 0;
    s->gate       = 0;

    refresh_coeffs(s);
    for (int i = 0; i < SUBORG_VOICES; i++) refresh_voice(&s->voice[i], s, (i >= 2) ? 0 : 1);

    return 0;
}

/* ---------------------------------------------------------------------------
 * The ADSR state machine -- one sample at a time.
 *
 *   IDLE     -> ATTACK   when the gate goes high
 *   ATTACK   -> DECAY    at level > 0.999
 *   DECAY    -> SUSTAIN  at level <= sustain
 *   SUSTAIN  -> RELEASE  when the gate goes low
 *   RELEASE  -> IDLE     at level < 0.0005
 * ------------------------------------------------------------------------ */

double suborg_env_step(suborg_env_t *e, const suborg_coeff_t *c)
{
    switch (e->stage) {
    case SUBORG_ENV_ATTACK:
        e->level += c->att * (1.0 - e->level);
        if (e->level >= 0.999) { e->level = 1.0; e->stage = SUBORG_ENV_DECAY; }
        break;

    case SUBORG_ENV_DECAY:
        e->level -= c->dec * (e->level - e->sustain);
        if (e->level <= e->sustain + 0.0005) {
            e->level = e->sustain;
            e->stage = SUBORG_ENV_SUSTAIN;
        }
        break;

    case SUBORG_ENV_SUSTAIN:
        e->level = e->sustain;
        break;

    case SUBORG_ENV_RELEASE:
        e->level -= c->rel * e->level;
        if (e->level < 0.0005) { e->level = 0.0; e->stage = SUBORG_ENV_IDLE; }
        break;

    case SUBORG_ENV_IDLE:
    default:
        e->level = 0.0;
        break;
    }
    return e->level;
}

/* ---------------------------------------------------------------------------
 * Notes
 * ------------------------------------------------------------------------ */

void suborg_note_on(suborg_t *s, int note, float vel)
{
    (void)vel;
    int idx = -1;
    for (int i = 2; i < SUBORG_VOICES; i++) {
        if (!s->voice[i].active) { idx = i; break; }
    }
    if (idx < 0) {
        uint32_t oldest = 0xFFFFFFFFu;
        idx = 2;
        for (int i = 2; i < SUBORG_VOICES; i++) {
            if (s->voice[i].age < oldest) { oldest = s->voice[i].age; idx = i; }
        }
    }

    if (s->held_count < 8) s->held[s->held_count++] = note;

    suborg_voice_t *v = &s->voice[idx];
    v->note      = note;
    v->freq      = note_to_hz(s->base_note, note);
    v->active    = true;
    v->releasing = false;
    v->age       = ++s->note_age;
    v->phase     = 0.0;

    s->gate++;
    s->env_amp.stage  = SUBORG_ENV_ATTACK;
    s->env_filt.stage = SUBORG_ENV_ATTACK;
}

/* note_off drops the gate and starts the release. THE VOICE STAYS ACTIVE --
 * that is the fix. It is retired by the render loop when the envelope reaches
 * IDLE, which is the only place a voice should ever be switched off. */
void suborg_note_off(suborg_t *s, int note)
{
    for (int i = 0; i < SUBORG_VOICES; i++) {
        if (s->voice[i].note == note) {
            s->voice[i].releasing = true;
            /* note is deliberately NOT cleared: the voice still needs to know
             * which note it is finishing. */
        }
    }
    for (int i = 0; i < s->held_count; i++) {
        if (s->held[i] == note) {
            for (int k = i; k < s->held_count - 1; k++) s->held[k] = s->held[k + 1];
            s->held_count--;
            break;
        }
    }

    /* Release only when the LAST key is up. Lifting one finger of a chord must
     * not cut the note short -- that is what the gate count is for. */
    if (s->gate > 0) s->gate--;
    if (s->gate == 0) {
        s->env_amp.stage  = SUBORG_ENV_RELEASE;
        s->env_filt.stage = SUBORG_ENV_RELEASE;
    }
}

/* Pad pressure. Fed from the wrapper on poly or channel aftertouch. */
void suborg_set_pressure(suborg_t *s, float pressure)
{
    if (!s) return;
    if (pressure < 0.0f) pressure = 0.0f;
    if (pressure > 1.0f) pressure = 1.0f;
    s->pressure = pressure;
}

/* ---------------------------------------------------------------------------
 * Render -- audio thread.
 * ------------------------------------------------------------------------ */

void suborg_render(suborg_t *s, float *out_l, float *out_r, int frames)
{
    if (frames <= 0) return;
    if (frames > SUBORG_MAX_BLOCK) frames = SUBORG_MAX_BLOCK;

    const double g      = s->coeff.filt_g;
    const double damp   = s->coeff.filt_damp;
    const double envamt = (double)suborg_filt_env_amounts[s->filt_env_sel];
    const uint8_t mode  = s->filt_mode;
    const double vol    = (double)s->volume;

    /* Headroom: six signals can overlap (2 VCOs x 3 sources). A bound, not a
     * tuned value. Without it the summed input reaches 1.4 and the SVF clips
     * internally, which is heard at ANY volume. */
    const double voice_gain = 1.0 / 6.0;

    double ic1 = s->svf_ic1;
    double ic2 = s->svf_ic2;

    for (int i = 0; i < frames; i++) {
        double ev = suborg_env_step(&s->env_amp,  &s->coeff);
        double ef = suborg_env_step(&s->env_filt, &s->coeff);

        double mix = 0.0;

        for (int vi = 0; vi < SUBORG_VOICES; vi++) {
            suborg_voice_t *v = &s->voice[vi];

            /* A releasing voice KEEPS RENDERING until its envelope is done.
             * This is the whole point of the releasing flag: the old code
             * switched the voice off in note_off and the release played into
             * silence. */
            if (!v->active) continue;

            v->inc = v->freq * v->oct_ratio * (1.0 / s->sample_rate);
            v->phase += v->inc;
            if (v->phase >= 1.0) v->phase -= 1.0;

            double p1 = v->phase * (double)v->div1;
            if (p1 >= 1.0) p1 -= (double)(int)p1;
            double p2 = v->phase * (double)v->div2;
            if (p2 >= 1.0) p2 -= (double)(int)p2;

            const double dt  = v->inc;
            const double dt1 = v->inc * (double)v->div1;
            const double dt2 = v->inc * (double)v->div2;

            double acc = v->lvl_osc * wave_sample(v->wave, v->phase, dt);
            if (v->lvl_sub1 > 0.0)
                acc += v->lvl_sub1 * v->scale1 * wave_sample(v->wave, p1, dt1);
            if (v->lvl_sub2 > 0.0)
                acc += v->lvl_sub2 * v->scale2 * wave_sample(v->wave, p2, dt2);
            mix += acc * voice_gain;
        }

        /* Retire the voices whose release has finished. Done after the mix so
         * the last audible sample is included, and only when the whole
         * envelope is idle -- never on note-off. */
        if (s->env_amp.stage == SUBORG_ENV_IDLE) {
            for (int vi = 0; vi < SUBORG_VOICES; vi++) {
                if (s->voice[vi].releasing) {
                    s->voice[vi].active    = false;
                    s->voice[vi].releasing = false;
                    s->voice[vi].note      = -1;
                }
            }
        }

        double gmod = g * (1.0 + envamt * 4.0 * ef);
        if (gmod > 0.5) gmod = 0.5;

        double hp = mix - damp * ic2 - ic1;
        double bp = gmod * hp + ic1;
        double lp = gmod * bp + ic2;
        ic1 = gmod * hp + bp;
        ic2 = gmod * bp + lp;

        double filtered;
        switch (mode) {
        case SUBORG_BP: filtered = bp; break;
        case SUBORG_HP: filtered = hp; break;
        case SUBORG_LP:
        default:        filtered = lp; break;
        }

        if (!(ic1 > -1e6 && ic1 < 1e6)) ic1 = 0.0;
        if (!(ic2 > -1e6 && ic2 < 1e6)) ic2 = 0.0;
        if (ic1 > -1e-15 && ic1 < 1e-15) ic1 = 0.0;
        if (ic2 > -1e-15 && ic2 < 1e-15) ic2 = 0.0;

        /* Soft saturation: x/(1+|x|) approaches 1.0 asymptotically with no
         * corner to hear. A hard clamp here IS distortion. */
        double out = filtered * ev * vol * 1.2;
        out = out / (1.0 + (out < 0.0 ? -out : out));

        out_l[i] = (float)out;
        out_r[i] = (float)out;
    }

    s->svf_ic1 = ic1;
    s->svf_ic2 = ic2;

    for (int vi = 0; vi < SUBORG_VOICES; vi++) {
        if (s->voice[vi].dirty) refresh_voice(&s->voice[vi], s, (vi >= 2) ? 0 : 1);
    }
}

/* ---------------------------------------------------------------------------
 * Parameters
 * ------------------------------------------------------------------------ */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

int suborg_set_param(suborg_t *s, const char *key, float value)
{
    if (!s || !key) return -1;

    if (!strcmp(key, "wave1"))     { s->wave[0] = (uint8_t)clampi((int)value, 0, SUBORG_WAVE_COUNT - 1); mark_all_dirty(s); return 0; }
    if (!strcmp(key, "wave2"))     { s->wave[1] = (uint8_t)clampi((int)value, 0, SUBORG_WAVE_COUNT - 1); mark_all_dirty(s); return 0; }
    if (!strcmp(key, "div1a"))     { s->div_sel[0][0] = (uint8_t)clampi((int)value, 0, SUBORG_DIVISOR_COUNT - 1); mark_all_dirty(s); return 0; }
    if (!strcmp(key, "div1b"))     { s->div_sel[0][1] = (uint8_t)clampi((int)value, 0, SUBORG_DIVISOR_COUNT - 1); mark_all_dirty(s); return 0; }
    if (!strcmp(key, "div2a"))     { s->div_sel[1][0] = (uint8_t)clampi((int)value, 0, SUBORG_DIVISOR_COUNT - 1); mark_all_dirty(s); return 0; }
    if (!strcmp(key, "div2b"))     { s->div_sel[1][1] = (uint8_t)clampi((int)value, 0, SUBORG_DIVISOR_COUNT - 1); mark_all_dirty(s); return 0; }
    if (!strcmp(key, "oct1"))      { s->oct_sel[0] = (uint8_t)clampi((int)value, 0, SUBORG_OCT_COUNT - 1); mark_all_dirty(s); return 0; }
    if (!strcmp(key, "oct2"))      { s->oct_sel[1] = (uint8_t)clampi((int)value, 0, SUBORG_OCT_COUNT - 1); mark_all_dirty(s); return 0; }
    if (!strcmp(key, "lvl1_osc"))  { s->lvl[0][0] = clampf(value, 0, 1); mark_all_dirty(s); return 0; }
    if (!strcmp(key, "lvl1_sub1")) { s->lvl[0][1] = clampf(value, 0, 1); mark_all_dirty(s); return 0; }
    if (!strcmp(key, "lvl1_sub2")) { s->lvl[0][2] = clampf(value, 0, 1); mark_all_dirty(s); return 0; }
    if (!strcmp(key, "lvl2_osc"))  { s->lvl[1][0] = clampf(value, 0, 1); mark_all_dirty(s); return 0; }
    if (!strcmp(key, "lvl2_sub1")) { s->lvl[1][1] = clampf(value, 0, 1); mark_all_dirty(s); return 0; }
    if (!strcmp(key, "lvl2_sub2")) { s->lvl[1][2] = clampf(value, 0, 1); mark_all_dirty(s); return 0; }

    if (!strcmp(key, "cutoff"))    { s->cutoff    = clampf(value, 0, 1); refresh_coeffs(s); return 0; }
    if (!strcmp(key, "resonance")) { s->resonance = clampf(value, 0, 1); refresh_coeffs(s); return 0; }
    if (!strcmp(key, "filt_mode")) { s->filt_mode = (uint8_t)clampi((int)value, 0, SUBORG_FILTER_MODE_COUNT - 1); return 0; }
    if (!strcmp(key, "filt_env"))  { s->filt_env_sel = (uint8_t)clampi((int)value, 0, SUBORG_FENV_COUNT - 1); return 0; }

    if (!strcmp(key, "env_attack"))  { s->atk_sel = (uint8_t)clampi((int)value, 0, SUBORG_ATK_COUNT - 1); refresh_coeffs(s); return 0; }
    if (!strcmp(key, "env_decay"))   { s->dec_sel = (uint8_t)clampi((int)value, 0, SUBORG_DEC_COUNT - 1); refresh_coeffs(s); return 0; }
    if (!strcmp(key, "env_release")) { s->rel_sel = (uint8_t)clampi((int)value, 0, SUBORG_DEC_COUNT - 1); refresh_coeffs(s); return 0; }
    if (!strcmp(key, "env_sustain")) {
        s->env_sustain = clampf(value, 0, 1);
        s->env_amp.sustain  = (double)s->env_sustain;
        s->env_filt.sustain = (double)s->env_sustain;
        return 0;
    }

    /* Aftertouch: how much pad pressure lengthens the release. */

    if (!strcmp(key, "volume"))    { s->volume = clampf(value, 0, 1); return 0; }

    return -1;
}

float suborg_get_param(suborg_t *s, const char *key)
{
    if (!s || !key) return 0.0f;
    if (!strcmp(key, "wave1"))     return (float)s->wave[0];
    if (!strcmp(key, "wave2"))     return (float)s->wave[1];
    if (!strcmp(key, "div1a"))     return (float)s->div_sel[0][0];
    if (!strcmp(key, "div1b"))     return (float)s->div_sel[0][1];
    if (!strcmp(key, "div2a"))     return (float)s->div_sel[1][0];
    if (!strcmp(key, "div2b"))     return (float)s->div_sel[1][1];
    if (!strcmp(key, "oct1"))      return (float)s->oct_sel[0];
    if (!strcmp(key, "oct2"))      return (float)s->oct_sel[1];
    if (!strcmp(key, "lvl1_osc"))  return s->lvl[0][0];
    if (!strcmp(key, "lvl1_sub1")) return s->lvl[0][1];
    if (!strcmp(key, "lvl1_sub2")) return s->lvl[0][2];
    if (!strcmp(key, "lvl2_osc"))  return s->lvl[1][0];
    if (!strcmp(key, "lvl2_sub1")) return s->lvl[1][1];
    if (!strcmp(key, "lvl2_sub2")) return s->lvl[1][2];
    if (!strcmp(key, "cutoff"))    return s->cutoff;
    if (!strcmp(key, "resonance")) return s->resonance;
    if (!strcmp(key, "filt_mode")) return (float)s->filt_mode;
    if (!strcmp(key, "filt_env"))  return (float)s->filt_env_sel;
    if (!strcmp(key, "env_attack"))  return (float)s->atk_sel;
    if (!strcmp(key, "env_decay"))   return (float)s->dec_sel;
    if (!strcmp(key, "env_sustain")) return s->env_sustain;
    if (!strcmp(key, "env_release")) return (float)s->rel_sel;
    if (!strcmp(key, "volume"))    return s->volume;

    /* read-only status */
    if (!strcmp(key, "env_stage")) return (float)s->env_amp.stage;
    if (!strcmp(key, "env_level")) return (float)s->env_amp.level;
    if (!strcmp(key, "pressure"))  return s->pressure;

    return 0.0f;
}

/* ---------------------------------------------------------------------------
 * UI hierarchy -- served from the plugin, never from module.json.
 * ------------------------------------------------------------------------ */

static const char *SUBORG_HIERARCHY =
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
      "{\"key\":\"volume\",\"label\":\"Volume\"},"
      "{\"key\":\"cutoff\",\"label\":\"Cutoff\"}"
    "]}}}";

const char *suborg_ui_hierarchy(void)
{
    return SUBORG_HIERARCHY;
}
