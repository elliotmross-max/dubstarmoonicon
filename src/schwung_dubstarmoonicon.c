#define _GNU_SOURCE 1
/* schwung_dubstarmoonicon.c -- the Schwung plugin wrapper for DubStarMooniCon.
 *
 * Written against src/host/plugin_api_v1.h. The v2 struct is:
 *
 *   typedef struct plugin_api_v2 {
 *       uint32_t api_version;
 *       void* (*create_instance)(const char *module_dir, const char *json_defaults);
 *       void (*destroy_instance)(void *instance);
 *       void (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
 *       void (*set_param)(void *instance, const char *key, const char *val);
 *       int  (*get_param)(void *instance, const char *key, char *buf, int buf_len);
 *       int  (*get_error)(void *instance, char *buf, int buf_len);
 *       void (*render_block)(void *instance, int16_t *out_interleaved_lr, int frames);
 *   } plugin_api_v2_t;
 *
 * TWO RULES THIS FILE FOLLOWS so the struct cannot drift:
 *   1. It does NOT restate plugin_api_v2_t -- it includes the host's header.
 *   2. It uses DESIGNATED INITIALIZERS: a misspelled field is a compile error,
 *      and an omitted field stays NULL rather than shifting the pointers after
 *      it. A positional initialiser missing one field calls the wrong function
 *      on the audio thread with no diagnostic.
 *
 * AFTERTOUCH
 *   Pad pressure arrives as polyphonic aftertouch (0xA0) from the pads, or as
 *   channel pressure (0xD0) over USB MIDI. Both are routed to
 *   suborg_set_pressure, where pressure LENGTHENS the release: at full
 *   pressure the release time is multiplied by up to 4x, so a 300 ms release
 *   becomes 1.2 s under a hard press.
 *
 *   Pressure is also cleared on all-notes-off, so a stuck value cannot leave
 *   the release stretched after a controller has gone quiet.
 *
 * THREADING CONTRACT: every entry point runs on the SPI audio callback --
 * SCHED_FIFO 70, pinned to core 3, roughly 2370 us of slack per 128-frame
 * block. No file I/O, no allocation, no blocking, no logging in any of them.
 */

#include "suborg.h"
#include "suborg_meta.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

#if defined(__linux__) && !defined(_GNU_SOURCE)
#endif

#if defined(__linux__)
#include <sched.h>
#endif

#if !defined(DSM_HOST_SIM)
#include "plugin_api_v1.h"

_Static_assert(offsetof(plugin_api_v2_t, get_error) != 0,
               "plugin_api_v2_t has no get_error field");
_Static_assert(offsetof(plugin_api_v2_t, render_block) >
               offsetof(plugin_api_v2_t, get_error),
               "plugin_api_v2_t: render_block must follow get_error");
#endif

typedef struct {
    suborg_t   engine;
    atomic_int ready;
    pthread_t  worker;
    int        worker_started;
    double     sample_rate;
    float      stage[SUBORG_MAX_BLOCK];
} dsm_instance_t;

/* The worker gets the instance and the rate on the HEAP. create_instance
 * returns before the worker is necessarily scheduled, so a stack argument
 * would be destroyed beneath it -- that bug cost several builds. The worker
 * frees it. */
typedef struct {
    dsm_instance_t *inst;
    double          sample_rate;
} dsm_worker_arg_t;

static int dsm_is_int_key(const char *key)
{
    static const char *ints[] = {
        "wave1", "wave2", "oct1", "oct2",
        "div1a", "div1b", "div2a", "div2b",
        NULL
    };
    for (int i = 0; ints[i]; i++) if (!strcmp(key, ints[i])) return 1;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Worker
 * ------------------------------------------------------------------------ */

static void *dsm_worker(void *arg)
{
    dsm_worker_arg_t *wa = (dsm_worker_arg_t *)arg;

    dsm_instance_t *inst = wa->inst;
    const double    rate = wa->sample_rate;
    free(wa);

#if defined(__linux__)
    /* Demote FIRST: threads inherit the creator's SCHED_FIFO priority, and a
     * FIFO 70 worker would starve Move's own Link Main at FIFO 35. */
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = 0;
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);

    /* Keep off core 3 -- that core belongs to the SPI callback. */
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    CPU_SET(1, &set);
    CPU_SET(2, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#endif

    int rc = suborg_build_tables(&inst->engine, rate, 440.0);

    atomic_store_explicit(&inst->ready, rc == 0 ? 1 : 0, memory_order_release);
    return NULL;
}

/* ---------------------------------------------------------------------------
 * v2 entry points
 * ------------------------------------------------------------------------ */

void *create_instance(const char *module_dir, const char *json_defaults)
{
    (void)module_dir;
    (void)json_defaults;

    dsm_instance_t *inst = (dsm_instance_t *)calloc(1, sizeof(*inst));
    if (!inst) return NULL;

    /* MOVE_SAMPLE_RATE is 44100. Assuming 48000 plays about a semitone and a
     * half sharp. */
    inst->sample_rate = 44100.0;
    atomic_store_explicit(&inst->ready, 0, memory_order_relaxed);

    dsm_worker_arg_t *wa = (dsm_worker_arg_t *)malloc(sizeof(*wa));
    if (!wa) { free(inst); return NULL; }
    wa->inst        = inst;
    wa->sample_rate = inst->sample_rate;

    if (pthread_create(&inst->worker, NULL, dsm_worker, wa) != 0) {
        free(wa);
        free(inst);
        return NULL;
    }
    inst->worker_started = 1;
    return inst;
}

void destroy_instance(void *instance)
{
    dsm_instance_t *inst = (dsm_instance_t *)instance;
    if (!inst) return;
    if (inst->worker_started) pthread_join(inst->worker, NULL);
    free(inst);
}

/* on_midi sees three message shapes that matter here:
 *   0x90/0x80  note on / note off
 *   0xA0       polyphonic aftertouch -- pad pressure, per note
 *   0xD0       channel pressure      -- pad pressure, whole channel
 *   0xB0 123   all notes off
 *
 * Poly aftertouch carries a note number in msg[1] and the pressure in msg[2];
 * channel pressure puts the pressure in msg[1]. The pads send the first, USB
 * controllers usually the second, so both are handled. */
void on_midi(void *instance, const uint8_t *msg, int len, int source)
{
    (void)source;
    dsm_instance_t *inst = (dsm_instance_t *)instance;
    if (!inst || !msg || len < 2) return;

    const uint8_t status = msg[0] & 0xF0;

    if (status == 0x90 && len >= 3) {
        if (msg[2] > 0) suborg_note_on(&inst->engine, msg[1], (float)msg[2] / 127.0f);
        else            suborg_note_off(&inst->engine, msg[1]);   /* vel 0 = off */

    } else if (status == 0x80 && len >= 3) {
        suborg_note_off(&inst->engine, msg[1]);
        /* A lifted finger is zero pressure for that note. */
        suborg_set_pressure(&inst->engine, 0.0f);

    } else if (status == 0xA0 && len >= 3) {
        /* Polyphonic aftertouch: pressure is in msg[2]. */
        suborg_set_pressure(&inst->engine, (float)msg[2] / 127.0f);

    } else if (status == 0xD0) {
        /* Channel pressure: pressure is in msg[1]. */
        suborg_set_pressure(&inst->engine, (float)msg[1] / 127.0f);

    } else if (status == 0xB0 && len >= 3 && msg[1] == 123) {
        for (int n = 0; n < 128; n++) suborg_note_off(&inst->engine, n);
        /* A controller that has gone quiet must not leave the release
         * stretched. */
        suborg_set_pressure(&inst->engine, 0.0f);
    }
}

/* The value arrives as a STRING. strtof is in libc: no syscall, no
 * allocation, safe on this thread. */
void set_param(void *instance, const char *key, const char *val)
{
    dsm_instance_t *inst = (dsm_instance_t *)instance;
    if (!inst || !key || !val) return;
    if (!strcmp(key, "ready") || !strcmp(key, "voices") ||
        !strcmp(key, "env_stage") || !strcmp(key, "env_level") ||
        !strcmp(key, "pressure")) return;                 /* read-only */
    (void)suborg_set_param(&inst->engine, key, strtof(val, NULL));
}

int get_param(void *instance, const char *key, char *buf, int buf_len)
{
    dsm_instance_t *inst = (dsm_instance_t *)instance;
    if (!key || !buf || buf_len <= 0) return -1;
    int n;

    if (!strcmp(key, "ui_hierarchy")) {
        n = snprintf(buf, (size_t)buf_len, "%s", SUBORG_UI_HIERARCHY);
        return (n < 0 || n >= buf_len) ? -1 : n;
    }
    if (!strcmp(key, "chain_params")) {
        n = snprintf(buf, (size_t)buf_len, "%s", SUBORG_CHAIN_PARAMS);
        return (n < 0 || n >= buf_len) ? -1 : n;
    }
    if (!strcmp(key, "ready")) {
        int r = inst ? atomic_load_explicit(&inst->ready, memory_order_acquire) : 0;
        n = snprintf(buf, (size_t)buf_len, "%d", r);
        return (n < 0 || n >= buf_len) ? -1 : n;
    }
    if (!strcmp(key, "voices")) {
        n = snprintf(buf, (size_t)buf_len, "%d", SUBORG_VOICES);
        return (n < 0 || n >= buf_len) ? -1 : n;
    }

    if (!inst) { buf[0] = '\0'; return 0; }

    const float v = suborg_get_param(&inst->engine, key);
    if (dsm_is_int_key(key))
        n = snprintf(buf, (size_t)buf_len, "%d", (int)(v < 0 ? v - 0.5f : v + 0.5f));
    else
        n = snprintf(buf, (size_t)buf_len, "%.4f", (double)v);

    return (n < 0 || n >= buf_len) ? -1 : n;
}

int get_error(void *instance, char *buf, int buf_len)
{
    dsm_instance_t *inst = (dsm_instance_t *)instance;
    if (!buf || buf_len <= 0) return -1;

    int ready = inst ? atomic_load_explicit(&inst->ready, memory_order_acquire) : 0;
    if (ready == 1) { buf[0] = '\0'; return 0; }

    int n = snprintf(buf, (size_t)buf_len, "engine building");
    return (n < 0 || n >= buf_len) ? -1 : n;
}

/* The engine renders mono float; the host wants interleaved int16 stereo.
 * Convert and clip once, here, at the boundary. */
void render_block(void *instance, int16_t *out_lr, int frames)
{
    dsm_instance_t *inst = (dsm_instance_t *)instance;
    if (!out_lr || frames <= 0) return;

    if (!inst || atomic_load_explicit(&inst->ready, memory_order_acquire) != 1) {
        for (int i = 0; i < frames * 2; i++) out_lr[i] = 0;
        return;
    }

    int n = frames;
    if (n > SUBORG_MAX_BLOCK) n = SUBORG_MAX_BLOCK;

    float *mono = inst->stage;
    suborg_render(&inst->engine, mono, mono, n);

    for (int i = 0; i < n; i++) {
        float s = mono[i];
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        int16_t v = (int16_t)(s * 32767.0f);
        out_lr[i * 2 + 0] = v;
        out_lr[i * 2 + 1] = v;
    }
    for (int i = n; i < frames; i++) {
        out_lr[i * 2 + 0] = 0;
        out_lr[i * 2 + 1] = 0;
    }
}

/* ---------------------------------------------------------------------------
 * Registration -- the only part that needs the host's types.
 * ------------------------------------------------------------------------ */

#if !defined(DSM_HOST_SIM)

static const plugin_api_v2_t dsm_api = {
    .api_version      = 2,
    .create_instance  = create_instance,
    .destroy_instance = destroy_instance,
    .on_midi          = on_midi,
    .set_param        = set_param,
    .get_param        = get_param,
    .get_error        = get_error,
    .render_block     = render_block,
};

plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host)
{
    (void)host;
    return (plugin_api_v2_t *)&dsm_api;
}

#endif /* !DSM_HOST_SIM */
