/* Audio: deterministic synth mixer + lock-free real-time streaming synth.
 * Tones, filtered noise, wet footsteps, grapple winch, sodium lamp hum,
 * subtle dockyard rain patter, and smooth soft-clipping WAV/PCM output. */
#include "engine.h"
#include <stdatomic.h>

void mixer_init(Mixer *m, int seconds) {
    m->cap = AU_RATE * seconds;
    m->len = m->cap;
    m->buf = calloc((size_t)m->cap, sizeof(float));
}
void mixer_free(Mixer *m) { free(m->buf); m->buf = NULL; }

static void add(Mixer *m, int i, float v) {
    if (i >= 0 && i < m->cap) m->buf[i] += v;
}

static inline float soft_clip(float x) {
    if (x >  2.5f) return  1.0f;
    if (x < -2.5f) return -1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

void au_tone(Mixer *m, float at, float dur, float f0, float f1, float gain) {
    int s0 = (int)(at * AU_RATE), n = (int)(dur * AU_RATE);
    if (n <= 0) return;
    float ph = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)n;
        float f = lerpf(f0, f1, t);
        ph += 2.0f * PS15_PI * f / AU_RATE;
        float env = sinf(PS15_PI * t);
        add(m, s0 + i, sinf(ph) * env * gain);
    }
}

void au_noise(Mixer *m, float at, float dur, float gain, uint32_t seed) {
    int s0 = (int)(at * AU_RATE), n = (int)(dur * AU_RATE);
    if (n <= 0) return;
    Rng rng = { seed ? seed : 1u };
    float lp = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)n;
        float white = rng_f01(&rng) * 2.0f - 1.0f;
        lp += 0.25f * (white - lp);
        float env = sinf(PS15_PI * t);
        add(m, s0 + i, lp * env * gain);
    }
}

void au_footstep_ex(Mixer *m, float at, bool wet, float gain_scale) {
    float g = clampf(gain_scale, 0.05f, 1.5f);
    au_noise(m, at, wet ? 0.13f : 0.085f, (wet ? 0.46f : 0.32f) * g,
             0x57E9u + (uint32_t)(at * 100.0f));
    if (wet) au_tone(m, at, 0.045f, 940.0f, 480.0f, 0.055f * g);
}

void au_footstep(Mixer *m, float at, bool wet) {
    au_footstep_ex(m, at, wet, 1.0f);
}

void au_grapple(Mixer *m, float at) {
    au_noise(m, at, 0.075f, 0.52f, 0x6A77u);
    au_tone(m, at + 0.03f, 0.24f, 280.0f, 1460.0f, 0.13f);
    au_tone(m, at + 0.24f, 0.06f, 1820.0f, 1200.0f, 0.08f);
}

void au_pistol_shot(Mixer *m, float at) {
    /* Subsonic suppressed sidearm: baffle hiss + low thump + slide cycle click */
    au_noise(m, at, 0.055f, 0.62f, 0x9157u + (uint32_t)(at * 50.0f));
    au_tone(m, at, 0.065f, 235.0f, 62.0f, 0.26f);
    au_tone(m, at + 0.045f, 0.035f, 2350.0f, 1380.0f, 0.09f);
}

void au_glass_break(Mixer *m, float at) {
    /* Shattering sodium streetlamp bulb + electric arc pop */
    au_noise(m, at, 0.14f, 0.58f, 0x61A55u);
    au_tone(m, at, 0.07f, 3200.0f, 1780.0f, 0.14f);
    au_tone(m, at + 0.04f, 0.09f, 2650.0f, 1120.0f, 0.11f);
    au_tone(m, at + 0.09f, 0.08f, 3600.0f, 2100.0f, 0.08f);
}

void au_visor_toggle(Mixer *m, float at) {
    /* Signature espionage night-optic capacitor charge whine */
    au_tone(m, at, 0.32f, 440.0f, 2680.0f, 0.11f);
    au_tone(m, at + 0.04f, 0.28f, 880.0f, 3560.0f, 0.05f);
}

void au_terminal_hack(Mixer *m, float at) {
    au_tone(m, at,        0.07f, 1320.0f, 1320.0f, 0.12f);
    au_tone(m, at + 0.08f, 0.07f, 1760.0f, 1760.0f, 0.12f);
    au_tone(m, at + 0.16f, 0.12f, 2350.0f, 2640.0f, 0.14f);
}

void au_mecha_step(Mixer *m, float at, float gain_scale) {
    float g = clampf(gain_scale, 0.0f, 1.5f);
    if (g <= 0.01f) return;
    au_tone(m, at, 0.22f, 92.0f, 36.0f, 0.28f * g);
    au_noise(m, at, 0.14f, 0.32f * g, 0x9EC4A1u + (uint32_t)(at * 50.0f));
    au_tone(m, at + 0.04f, 0.16f, 320.0f, 210.0f, 0.07f * g);
}

void au_hum(Mixer *m, float dur) {
    int n = (int)(dur * AU_RATE);
    Rng rain_rng = { 0x9A17BEDu };
    float rain_lp = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / AU_RATE;
        float hum = (sinf(2.0f * PS15_PI * 120.0f * t) * 0.50f +
                     sinf(2.0f * PS15_PI * 240.0f * t) * 0.20f +
                     sinf(2.0f * PS15_PI * 360.0f * t) * 0.08f) * 0.026f;
        /* Subtle moody espionage dark bass pulse (A1 = 55 Hz -> E1 = 41.2 Hz) */
        float bar_pos = fmodf(t, 4.0f);
        float bass_f = (bar_pos < 2.5f) ? 55.0f : 41.203f;
        float pulse_env = expf(-fmodf(t, 1.0f) * 3.2f) * 0.036f;
        float bass = sinf(2.0f * PS15_PI * bass_f * t) * pulse_env;

        float white = rng_f01(&rain_rng) * 2.0f - 1.0f;
        rain_lp += 0.12f * (white - rain_lp);
        add(m, i, hum + bass + (white - rain_lp) * 0.012f);
    }
}

void au_positional(float dist, float *gain, float *pan) {
    *gain = clampf(1.0f / (1.0f + dist * 0.22f), 0.0f, 1.0f);
    *pan = 0.0f;
}

/* ---------- thread-safe live streaming synth -------------------------- */
void au_live_init(AudioLive *a) {
    memset(a, 0, sizeof *a);
    a->hum_on = 1;
}

static LiveVoice *voice_alloc(AudioLive *a) {
    for (int i = 0; i < AU_MAX_VOICES; i++) {
        int idx = (int)((a->next_slot + (uint32_t)i) % AU_MAX_VOICES);
        if (!__atomic_load_n(&a->voices[idx].active, __ATOMIC_ACQUIRE)) {
            a->next_slot = (uint32_t)(idx + 1);
            return &a->voices[idx];
        }
    }
    int idx = (int)(a->next_slot++ % AU_MAX_VOICES);
    __atomic_store_n(&a->voices[idx].active, 0, __ATOMIC_RELEASE);
    return &a->voices[idx];
}

void au_live_tone(AudioLive *a, float dur, float f0, float f1, float gain) {
    if (dur <= 1e-4f) return;
    LiveVoice *v = voice_alloc(a);
    v->kind = 0;
    v->t = 0.0f;
    v->dur = dur;
    v->f0 = f0;
    v->f1 = f1;
    v->gain = gain;
    v->phase = 0.0f;
    v->lp = 0.0f;
    v->rng = 1u;
    __atomic_store_n(&v->active, 1, __ATOMIC_RELEASE);
}

void au_live_noise(AudioLive *a, float dur, float gain, uint32_t seed) {
    if (dur <= 1e-4f) return;
    LiveVoice *v = voice_alloc(a);
    v->kind = 1;
    v->t = 0.0f;
    v->dur = dur;
    v->f0 = 0.0f;
    v->f1 = 0.0f;
    v->gain = gain;
    v->phase = 0.0f;
    v->lp = 0.0f;
    v->rng = seed ? seed : 1u;
    __atomic_store_n(&v->active, 1, __ATOMIC_RELEASE);
}

void au_live_footstep(AudioLive *a, int wet) {
    au_live_noise(a, wet ? 0.13f : 0.085f, wet ? 0.45f : 0.32f,
                  0x57E9u + (uint32_t)(a->time * 100.0f));
    if (wet) au_live_tone(a, 0.045f, 940.0f, 480.0f, 0.055f);
}

void au_live_grapple(AudioLive *a) {
    au_live_noise(a, 0.075f, 0.50f, 0x6A77u);
    au_live_tone(a, 0.24f, 280.0f, 1460.0f, 0.13f);
}

void au_live_pistol(AudioLive *a) {
    au_live_noise(a, 0.055f, 0.60f, 0x9157u + (uint32_t)(a->time * 50.0f));
    au_live_tone(a, 0.065f, 235.0f, 62.0f, 0.25f);
    au_live_tone(a, 0.035f, 2350.0f, 1380.0f, 0.09f);
}

void au_live_glass(AudioLive *a) {
    au_live_noise(a, 0.14f, 0.56f, 0x61A55u);
    au_live_tone(a, 0.08f, 3200.0f, 1650.0f, 0.14f);
    au_live_tone(a, 0.10f, 2500.0f, 1080.0f, 0.10f);
}

void au_live_visor(AudioLive *a) {
    au_live_tone(a, 0.30f, 440.0f, 2680.0f, 0.11f);
    au_live_tone(a, 0.26f, 880.0f, 3560.0f, 0.05f);
}

void au_live_hack(AudioLive *a) {
    au_live_tone(a, 0.08f, 1320.0f, 1760.0f, 0.12f);
    au_live_tone(a, 0.14f, 1760.0f, 2540.0f, 0.12f);
}

void au_live_mecha_step(AudioLive *a, float gain_scale) {
    float g = clampf(gain_scale, 0.0f, 1.5f);
    if (g <= 0.01f) return;
    au_live_tone(a, 0.22f, 92.0f, 36.0f, 0.26f * g);
    au_live_noise(a, 0.14f, 0.30f * g, 0x9EC4A1u + (uint32_t)(a->time * 50.0f));
    au_live_tone(a, 0.15f, 320.0f, 210.0f, 0.06f * g);
}

void au_live_render(AudioLive *a, float *out, int n) {
    uint32_t rain_s = 0x9A17BEDu ^ (uint32_t)(a->time * 22050.0f);
    if (!rain_s) rain_s = 1u;
    float rain_lp = 0.0f;

    for (int i = 0; i < n; i++) {
        float t = a->time + (float)i / AU_RATE;
        float s = 0.0f;
        if (a->hum_on) {
            s += (sinf(2.0f * PS15_PI * 120.0f * t) * 0.50f +
                  sinf(2.0f * PS15_PI * 240.0f * t) * 0.20f) * 0.024f;
            float bar_pos = fmodf(t, 4.0f);
            float bass_f = (bar_pos < 2.5f) ? 55.0f : 41.203f;
            float pulse_env = expf(-fmodf(t, 1.0f) * 3.2f) * 0.032f;
            s += sinf(2.0f * PS15_PI * bass_f * t) * pulse_env;
            rain_s ^= rain_s << 13; rain_s ^= rain_s >> 17; rain_s ^= rain_s << 5;
            float w = ((rain_s >> 9) * (1.0f / 8388608.0f)) - 1.0f;
            rain_lp += 0.12f * (w - rain_lp);
            s += (w - rain_lp) * 0.010f;
        }
        for (int k = 0; k < AU_MAX_VOICES; k++) {
            LiveVoice *v = &a->voices[k];
            if (!__atomic_load_n(&v->active, __ATOMIC_ACQUIRE)) continue;
            float dur = v->dur;
            if (dur <= 1e-4f) continue;
            float lt = v->t + (float)i / AU_RATE;
            if (lt >= dur) continue;
            float u = lt / dur;
            float env = sinf(PS15_PI * u);
            if (v->kind == 0) {
                float f = lerpf(v->f0, v->f1, u);
                v->phase += 2.0f * PS15_PI * f / AU_RATE;
                s += sinf(v->phase) * env * v->gain;
            } else {
                uint32_t r = v->rng ? v->rng : 1u;
                r ^= r << 13; r ^= r >> 17; r ^= r << 5;
                v->rng = r;
                float white = ((r >> 9) * (1.0f / 8388608.0f)) - 1.0f;
                v->lp += 0.25f * (white - v->lp);
                s += v->lp * env * v->gain;
            }
        }
        out[i] = soft_clip(s);
    }

    float dt = (float)n / AU_RATE;
    a->time += dt;
    for (int k = 0; k < AU_MAX_VOICES; k++) {
        LiveVoice *v = &a->voices[k];
        if (!__atomic_load_n(&v->active, __ATOMIC_ACQUIRE)) continue;
        v->t += dt;
        if (v->t >= v->dur) __atomic_store_n(&v->active, 0, __ATOMIC_RELEASE);
    }
}

int au_write_wav(const char *path, const Mixer *m) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t data = (uint32_t)m->len * 2u;
    uint32_t chunk = 36u + data;
    uint32_t sub = 16u, rate = AU_RATE, brate = AU_RATE * 2u;
    uint16_t fmt = 1u, ch = 1u, align = 2u, bits = 16u;

    fwrite("RIFF", 1, 4, f);
    fwrite(&chunk, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&sub, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data, 4, 1, f);

    for (int i = 0; i < m->len; i++) {
        float v = soft_clip(m->buf[i]);
        int16_t s = (int16_t)(clampf(v, -1.0f, 1.0f) * 30000.0f);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
    return 0;
}
