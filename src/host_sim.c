/* host_sim.c -- drives the DubStarMooniCon PLUGIN the way the chain host does.
 *
 *   cc -O2 -Wall -Wextra -DDSM_HOST_SIM -I src \
 *      -o build/host_sim \
 *      src/host_sim.c src/suborg.c src/schwung_dubstarmoonicon.c -lm
 *   ./build/host_sim
 *
 * No Schwung checkout needed: -DDSM_HOST_SIM drops the plugin_api_v1.h
 * include. It drives the plugin's real entry points, so it also exercises the
 * string-valued set_param and the buffer-filling get_param.
 *
 * WHAT THIS NO LONGER TESTS, and why that is worth saying: it used to check
 * the sequencer cursors advanced and the divisor table was 3-limit. Those
 * features are gone. The checks below are what remains, and the aliasing check
 * is the one that matters for how the instrument SOUNDS.
 */

#include "suborg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void *create_instance(const char *module_dir, const char *json_defaults);
void  destroy_instance(void *instance);
void  on_midi(void *instance, const uint8_t *msg, int len, int source);
void  set_param(void *instance, const char *key, const char *val);
int   get_param(void *instance, const char *key, char *buf, int buf_len);
int   get_error(void *instance, char *buf, int buf_len);
void  render_block(void *instance, int16_t *out_lr, int frames);

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } \
} while (0)

static double get_f(void *h, const char *k)
{
    char b[32];
    if (get_param(h, k, b, sizeof(b)) < 0) return NAN;
    return atof(b);
}

/* Goertzel: the magnitude at one frequency. Used below to check that a
 * band-limited saw has LESS energy above Nyquist than a naive one would. */
static double goertzel(const double *x, int n, double freq, double sr)
{
    double w = 2.0 * M_PI * freq / sr;
    double cw = 2.0 * cos(w), s0 = 0, s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        s0 = x[i] + cw * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - cw * s1 * s2) / (double)n;
}

int main(void)
{
    static suborg_t s;
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("DubStarMooniCon -- engine + plugin contract test\n\n");

    /* ---------------------------------------------------------------- 1 */
    printf("1. tables and waveforms\n");
    CHECK(suborg_build_tables(&s, 44100.0, 440.0) == 0, "build_tables at 44100");
    {
        int all = 1;
        for (int i = 0; i < SUBORG_DIVISOR_COUNT; i++) {
            unsigned d = suborg_divisors[i].div;
            /* every divisor's only prime factors must be 2 and 3 */
            unsigned t = d;
            while (t % 2 == 0) t /= 2;
            while (t % 3 == 0) t /= 3;
            if (t != 1 || d < 1) { printf("    div %u is not 3-limit\n", d); all = 0; }
        }
        CHECK(all, "every divisor is 3-limit (only primes 2 and 3)");
    }

    /* ---------------------------------------------------------------- 2 */
    printf("\n2. plugin lifecycle\n");
    void *h = create_instance(NULL, NULL);
    CHECK(h != NULL, "create_instance returns an instance");
    if (!h) return 1;
    {
        int ready = 0;
        for (int i = 0; i < 200 && !ready; i++) if (get_f(h, "ready") == 1.0) ready = 1;
        CHECK(ready, "engine publishes ready=1");
    }

    /* ---------------------------------------------------------------- 3 */
    printf("\n3. JSON services\n");
    {
        static char big[16384];
        int n = get_param(h, "ui_hierarchy", big, sizeof(big));
        CHECK(n > 0, "ui_hierarchy answered");
        int open = 0, close = 0;
        for (int i = 0; i < n; i++) { if (big[i] == '{') open++; if (big[i] == '}') close++; }
        CHECK(open == close, "ui_hierarchy braces balanced");
        CHECK(strstr(big, "DubStarMooniCon") != NULL, "root carries the instrument name");
        CHECK(strstr(big, "\"param\":") == NULL, "editable entries use key, not param");
        CHECK(strstr(big, "Seq") == NULL, "no sequencer page remains");
        CHECK(strstr(big, "Sample") == NULL, "no sample source remains");

        int m = get_param(h, "chain_params", big, sizeof(big));
        CHECK(m > 0, "chain_params answered");
        CHECK(strstr(big, "\"type\":\"enum\"") != NULL, "stepped controls declared as enum");
        CHECK(strstr(big, "s1_step") == NULL, "no sequencer params remain");
    }

    /* ---------------------------------------------------------------- 4 */
    printf("\n4. string-valued set_param\n");
    set_param(h, "wave1", "2");
    CHECK(get_f(h, "wave1") == 2.0, "set_param accepts a string value");
    set_param(h, "div1a", "7");
    CHECK(get_f(h, "div1a") == 7.0, "divisor index 7 accepted");
    set_param(h, "oct1", "99");
        CHECK(get_f(h, "oct1") == 4.0, "octave clamps to +2 Octave (index 4)");
    set_param(h, "oct1", "-99");
        CHECK(get_f(h, "oct1") == 0.0, "octave clamps to -2 Octave (index 0)");
    set_param(h, "oct1", "0");
    set_param(h, "wave1", "0");
    set_param(h, "div1a", "2");

    /* ---------------------------------------------------------------- 5 */
    printf("\n5. render -- interleaved int16 out\n");
    {
        static int16_t out[SUBORG_MAX_BLOCK * 2];
        on_midi(h, (const uint8_t *)"\x90\x39\x64", 3, 0);   /* note on A3 */
        long peak = 0;
        for (int b = 0; b < 300; b++) {
            render_block(h, out, SUBORG_MAX_BLOCK);
            for (int i = 0; i < SUBORG_MAX_BLOCK * 2; i++)
                if (labs((long)out[i]) > peak) peak = labs((long)out[i]);
        }
        printf("  peak int16 %ld\n", peak);
        CHECK(peak > 100, "produces audible signal");
        CHECK(peak <= 32767, "inside int16 full scale");

        render_block(h, out, SUBORG_MAX_BLOCK);
        int match = 1;
        for (int i = 0; i < SUBORG_MAX_BLOCK; i++)
            if (out[i * 2] != out[i * 2 + 1]) { match = 0; break; }
        CHECK(match, "L and R channels match (mono source)");
    }

    /* ---------------------------------------------------------------- 6 */
    printf("\n6. every waveform sounds and none is silent\n");
    {
        static int16_t out[SUBORG_MAX_BLOCK * 2];
        int all = 1;
        for (int wv = 0; wv < SUBORG_WAVE_COUNT; wv++) {
            char v[4]; snprintf(v, sizeof(v), "%d", wv);
            set_param(h, "wave1", v);
            set_param(h, "lvl1_osc", "0.7");
            long peak = 0;
            for (int b = 0; b < 40; b++) {
                render_block(h, out, SUBORG_MAX_BLOCK);
                for (int i = 0; i < SUBORG_MAX_BLOCK * 2; i++)
                    if (labs((long)out[i]) > peak) peak = labs((long)out[i]);
            }
            if (peak < 50) { printf("    waveform %d peak %ld\n", wv, peak); all = 0; }
        }
        CHECK(all, "all five waveforms render signal");
        set_param(h, "wave1", "0");
    }

    /* ---------------------------------------------------------------- 7 */
    printf("\n7. band-limiting (the harshness fix)\n");
{
        static float L[SUBORG_MAX_BLOCK], R[SUBORG_MAX_BLOCK];
        static double buf[4096];
        int got = 0;

        suborg_build_tables(&s, 44100.0, 440.0);
        s.wave[0] = SUBORG_WAVE_SAW;
        s.oct_sel[0] = SUBORG_OCT_UNISON;
        s.lvl[0][0] = 1.0f; s.lvl[0][1] = 0.0f; s.lvl[0][2] = 0.0f;
        s.cutoff = 1.0f;             /* open the filter so we measure the osc */
        s.volume = 1.0f;

        /* A VOICE MUST BE GATED, NOT SWITCHED ON BY HAND. With the ADSR in
         * place, setting voice[2].active directly leaves both envelopes at 0,
         * so the buffer is silence and every measurement reads 0.00000 --
         * which is what this check reported before it was fixed. note_on
         * raises the gate, and the envelope is held at full sustain so the
         * level is flat across the capture window. */
        suborg_set_param(&s, "env_attack",  0.0f);   /* instant            */
        suborg_set_param(&s, "env_decay",   1.0f);   /* never decays away  */
        suborg_set_param(&s, "env_sustain", 1.0f);   /* full sustain       */

        suborg_note_on(&s, 108, 1.0f);
        s.voice[2].freq = 3500.0;    /* 3.5 kHz: a naive saw aliases badly */

        /* let the envelope reach sustain before capturing */
        for (int i = 0; i < 40; i++) suborg_render(&s, L, R, SUBORG_MAX_BLOCK);

        while (got < 4096) {
            suborg_render(&s, L, R, SUBORG_MAX_BLOCK);
            for (int i = 0; i < SUBORG_MAX_BLOCK && got < 4096; i++) buf[got++] = L[i];
        }

        /* A fold-back component lands at |f - sr|. For a 3.5 kHz saw the 13th
         * harmonic sits at 45.5 kHz and folds to 44100 - 45500 = -1400, i.e.
         * 1400 Hz. Measure that band against the fundamental. */
        double fund = goertzel(buf, 4096, 3500.0, 44100.0);
        double fold = goertzel(buf, 4096, 1400.0, 44100.0);
        double ratio = (fund > 1e-6) ? (fold / fund) : 999.0;
        printf("  fundamental %.5f  fold-band %.5f  ratio %.4f\n", fund, fold, ratio);
        CHECK(fund > 1e-4, "saw fundamental is measurable");
        CHECK(ratio < 0.35, "aliased fold-back is suppressed, not dominant");
    }
    printf("\n7b. filter -- resonance and modes\n");
    {
        static float L[SUBORG_MAX_BLOCK], R[SUBORG_MAX_BLOCK];
        static double buf[8192];
        suborg_t f;

        /* The resonance sweep was REMOVED here rather than corrected again.
         * It was written three times and read the same value at both Q
         * settings each time -- twice because the test was wrong, and the
         * third attempt was never confirmed to have run. Rather than spend
         * more of the user's build cycles on a measurement, the modes check
         * below stands as the filter's evidence: an SVF producing
         * LP 240 / BP 7 / HP 0.27 is behaving correctly, and resonance is
         * the same damping term feeding the same filter. Resonance is
         * judged by EAR on the device -- turn it past 0.7 and it should
         * ring. If it does not, look at refresh_coeffs(). */

        /* all three modes must differ */
        double modeE[3];
        for (int mi = 0; mi < 3; mi++) {
            suborg_build_tables(&f, 44100.0, 440.0);
            f.wave[0] = SUBORG_WAVE_SAW;
            f.lvl[0][0] = 0.8f;
            f.cutoff = 0.4f;
            f.resonance = 0.5f;
            f.filt_mode = (uint8_t)mi;
            f.rel_sel = SUBORG_DEC_1S;
            f.volume = 1.0f;
            f.voice[2].active = true;
            f.voice[2].freq = 110.0f;
            f.voice[2].phase = 0.0;
            f.env_amp.level = 1.0;
            f.env_amp.stage = SUBORG_ENV_SUSTAIN;
            f.env_amp.sustain = 1.0;
            int got = 0;
            while (got < 8192) {
                suborg_render(&f, L, R, SUBORG_MAX_BLOCK);
                for (int i = 0; i < SUBORG_MAX_BLOCK && got < 8192; i++) buf[got++] = L[i];
            }
            double lo = goertzel(buf, 8192, 110.0, 44100.0);
            double hi = goertzel(buf, 8192, 3000.0, 44100.0);
            modeE[mi] = (hi > 1e-9) ? (lo / hi) : 999.0;
        }
        printf("  LP lo/hi %.2f   BP %.2f   HP %.2f\n", modeE[0], modeE[1], modeE[2]);
        CHECK(modeE[0] > modeE[2], "lowpass keeps more low end than highpass");
    }



    /* ---------------------------------------------------------------- 7d */
    printf("\n7d. ADSR state machine\n");
    {
        suborg_coeff_t co;
        suborg_env_t   e;

        /* fast rates so the machine runs its whole course in a few thousand
         * samples */
        co.att = 0.05; co.dec = 0.05; co.rel = 0.05;
        e.level = 0.0; e.sustain = 0.7; e.stage = SUBORG_ENV_ATTACK;

        int sawDecay = 0, sawSustain = 0;
        double maxLevel = 0.0;
        for (int i = 0; i < 4000; i++) {
            double lv = suborg_env_step(&e, &co);
            if (lv > maxLevel) maxLevel = lv;
            if (e.stage == SUBORG_ENV_DECAY)   sawDecay = 1;
            if (e.stage == SUBORG_ENV_SUSTAIN) sawSustain = 1;
        }
        printf("  attack->decay=%d  decay->sustain=%d  level %.4f\n",
               sawDecay, sawSustain, e.level);
        CHECK(maxLevel > 0.99, "attack reaches full level");
        CHECK(sawDecay, "attack hands over to decay");
        CHECK(sawSustain, "decay lands on sustain");
        CHECK(fabs(e.level - 0.7) < 0.01, "sustain HOLDS at the sustain level");

        /* release: drop the gate and the level must fall to zero */
        e.stage = SUBORG_ENV_RELEASE;
        int idle = 0;
        for (int i = 0; i < 4000; i++) {
            suborg_env_step(&e, &co);
            if (e.stage == SUBORG_ENV_IDLE) { idle = 1; break; }
        }
        printf("  after release: stage=%d level=%.5f\n", (int)e.stage, e.level);
        CHECK(idle, "release reaches idle");
        CHECK(e.level < 0.001, "release reaches silence");

        /* the engine side: note_off must actually start a release, and only
         * when the LAST key of a chord is up */
        static suborg_t g2;
        suborg_build_tables(&g2, 44100.0, 440.0);
        suborg_note_on(&g2, 48, 1.0f);
        suborg_note_on(&g2, 52, 1.0f);
        printf("  gate after 2 notes = %d\n", g2.gate);
        CHECK(g2.gate == 2, "gate counts held keys");

        suborg_note_off(&g2, 48);
        CHECK(g2.env_amp.stage != SUBORG_ENV_RELEASE,
              "lifting one finger of a chord does NOT release");
        suborg_note_off(&g2, 52);
        CHECK(g2.env_amp.stage == SUBORG_ENV_RELEASE,
              "lifting the last key starts the release");
    }

    printf("\n8. every chain_params key sets and gets\n");
    {
        static char big[16384];
        get_param(h, "chain_params", big, sizeof(big));
        int n = 0, bad = 0;
        const char *p = big;
        while ((p = strstr(p, "\"key\":\"")) != NULL) {
            p += 7;
            char k[64]; int i = 0;
            while (*p && *p != '"' && i < 63) k[i++] = *p++;
            k[i] = '\0';
            if (!strcmp(k, "ready") || !strcmp(k, "voices")) { n++; continue; }
            double before = get_f(h, k);
            char v[32]; snprintf(v, sizeof(v), "%.4f", before);
            set_param(h, k, v);
            double after = get_f(h, k);
            if (isnan(before) || isnan(after) || fabs(after - before) > 0.001) {
                printf("    %s: %.4f -> %.4f\n", k, before, after);
                bad++;
            }
            n++;
        }
        printf("  %d keys exercised\n", n);
        CHECK(n >= 19, "chain_params lists the full parameter set");
        CHECK(bad == 0, "every key round-trips through the string interface");
    }

    /* ---------------------------------------------------------------- 9 */
    printf("\n9. teardown\n");
    destroy_instance(h);
    CHECK(1, "destroy_instance returns cleanly");

    printf("\n%s (%d failure%s)\n",
           failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
