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
