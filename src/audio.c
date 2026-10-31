/* Audio: deterministic synth mixer. Tones, filtered noise, footsteps,
 * grapple, lamp hum — mixed to PCM and written as a WAV. No device
 * backend in the headless host; the mix IS the engine's sound output. */
#include "engine.h"

void mixer_init(Mixer *m, int seconds) {
    m->cap = AU_RATE * seconds;
    m->len = m->cap;
    m->buf = calloc((size_t)m->cap, sizeof(float));
}
void mixer_free(Mixer *m) { free(m->buf); }

static void add(Mixer *m, int i, float v) {
    if (i >= 0 && i < m->cap) m->buf[i] += v;
}

void au_tone(Mixer *m, float at, float dur, float f0, float f1, float gain) {
    int s0 = (int)(at * AU_RATE), n = (int)(dur * AU_RATE);
    float ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        float f = lerpf(f0, f1, t);
        ph += 2.0f * 3.14159265f * f / AU_RATE;
        float env = sinf(3.14159265f * t);
        add(m, s0 + i, sinf(ph) * env * gain);
    }
}

void au_noise(Mixer *m, float at, float dur, float gain, uint32_t seed) {
    int s0 = (int)(at * AU_RATE), n = (int)(dur * AU_RATE);
    Rng rng = { seed ? seed : 1 };
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        float white = rng_f01(&rng) * 2 - 1;
        lp += 0.25f * (white - lp);            /* cheap lowpass */
        float env = sinf(3.14159265f * t);
        add(m, s0 + i, lp * env * gain);
    }
}

void au_footstep(Mixer *m, float at, bool wet) {
    au_noise(m, at, wet ? 0.14f : 0.09f, wet ? 0.5f : 0.35f,
             0x57E9u + (uint32_t)(at * 100));
    if (wet) au_tone(m, at, 0.05f, 900, 500, 0.06f);  /* splash tick */
}

void au_grapple(Mixer *m, float at) {
    au_noise(m, at, 0.08f, 0.5f, 0x6A77u);            /* launch thwip */
    au_tone(m, at + 0.05f, 0.22f, 300, 1400, 0.12f);  /* reel whirr up */
}

void au_hum(Mixer *m, float dur) {
    int n = (int)(dur * AU_RATE);
    for (int i = 0; i < n; i++) {
        float t = (float)i / AU_RATE;
        add(m, i, (sinf(2 * 3.14159265f * 120 * t) * 0.5f +
                   sinf(2 * 3.14159265f * 240 * t) * 0.2f) * 0.035f);
    }
}

void au_positional(float dist, float *gain, float *pan) {
    *gain = clampf(1.0f / (1.0f + dist * 0.25f), 0, 1);
    *pan = 0; /* mono headless mix; pan is computed for stereo backends */
}

/* ---------- live streaming synth (device backends pull from this) --- */
void au_live_init(AudioLive *a) {
    memset(a, 0, sizeof *a);
    a->hum_on = 1;
}
static LiveVoice *voice_new(AudioLive *a) {
    for (int i = 0; i < AU_MAX_VOICES; i++)
        if (!a->voices[i].active) return &a->voices[i];
    return &a->voices[0];           /* steal oldest slot when full */
}
void au_live_tone(AudioLive *a, float dur, float f0, float f1, float gain) {
    LiveVoice *v = voice_new(a);
    *v = (LiveVoice){ 1, 0, 0, dur, f0, f1, gain, 0, 0, 1 };
}
void au_live_noise(AudioLive *a, float dur, float gain, uint32_t seed) {
    LiveVoice *v = voice_new(a);
    *v = (LiveVoice){ 1, 1, 0, dur, 0, 0, gain, 0, 0, seed ? seed : 1 };
}
void au_live_footstep(AudioLive *a, int wet) {
    au_live_noise(a, wet ? 0.14f : 0.09f, wet ? 0.5f : 0.35f,
                  0x57E9u + (uint32_t)(a->time * 100));
    if (wet) au_live_tone(a, 0.05f, 900, 500, 0.06f);
}
void au_live_grapple(AudioLive *a) {
    au_live_noise(a, 0.08f, 0.5f, 0x6A77u);
    au_live_tone(a, 0.22f, 300, 1400, 0.12f);
}
void au_live_render(AudioLive *a, float *out, int n) {
    for (int i = 0; i < n; i++) {
        float t = a->time + (float)i / AU_RATE;
        float s = 0;
        if (a->hum_on)
            s += (sinf(2 * 3.14159265f * 120 * t) * 0.5f +
                  sinf(2 * 3.14159265f * 240 * t) * 0.2f) * 0.035f;
        for (int k = 0; k < AU_MAX_VOICES; k++) {
            LiveVoice *v = &a->voices[k];
            if (!v->active) continue;
            float lt = v->t + (float)i / AU_RATE;
            if (lt >= v->dur) continue;
            float env = sinf(3.14159265f * lt / v->dur);
            if (v->kind == 0) {
                float f = lerpf(v->f0, v->f1, lt / v->dur);
                v->phase += 2.0f * 3.14159265f * f / AU_RATE;
                s += sinf(v->phase) * env * v->gain;
            } else {
                v->rng ^= v->rng << 13; v->rng ^= v->rng >> 17; v->rng ^= v->rng << 5;
                float white = ((v->rng >> 9) / 8388608.0f) - 1.0f;
                v->lp += 0.25f * (white - v->lp);
                s += v->lp * env * v->gain;
            }
        }
        out[i] = s;
    }
    a->time += (float)n / AU_RATE;
    for (int k = 0; k < AU_MAX_VOICES; k++) {
        LiveVoice *v = &a->voices[k];
        if (!v->active) continue;
        v->t += (float)n / AU_RATE;
        if (v->t >= v->dur) v->active = 0;
    }
}

int au_write_wav(const char *path, const Mixer *m) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int data = m->len * 2;
    fwrite("RIFF", 1, 4, f);
    uint32_t chunk = 36 + (uint32_t)data;
    fwrite(&chunk, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    uint32_t sub = 16; fwrite(&sub, 4, 1, f);
    uint16_t fmt = 1, ch = 1, bits = 16;
    uint32_t rate = AU_RATE, brate = AU_RATE * 2;
    uint16_t align = 2;
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f);
    fwrite(&brate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data, 4, 1, f);
    for (int i = 0; i < m->len; i++) {
        float v = clampf(m->buf[i], -1, 1);
        int16_t s = (int16_t)(v * 30000);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
    return 0;
}
