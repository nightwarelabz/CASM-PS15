/* ALSA playback backend: a thread pulls the live synth into the
 * default PCM device as 16-bit mono at AU_RATE. */
#define _POSIX_C_SOURCE 200809L
#include "engine.h"
#include "platform.h"
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <stdatomic.h>

static AudioLive *g_live;
static pthread_t g_thread;
static atomic_int g_run;
static snd_pcm_t *g_pcm;

static void *pump(void *unused) {
    (void)unused;
    float fbuf[1024];
    int16_t sbuf[1024];
    while (atomic_load(&g_run)) {
        au_live_render(g_live, fbuf, 1024);
        for (int i = 0; i < 1024; i++)
            sbuf[i] = (int16_t)(clampf(fbuf[i], -1, 1) * 30000);
        snd_pcm_sframes_t w = snd_pcm_writei(g_pcm, sbuf, 1024);
        if (w < 0) snd_pcm_recover(g_pcm, (int)w, 1);
    }
    return NULL;
}

int audev_start(AudioLive *live) {
    if (snd_pcm_open(&g_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
        return -1;
    if (snd_pcm_set_params(g_pcm, SND_PCM_FORMAT_S16_LE,
                           SND_PCM_ACCESS_RW_INTERLEAVED, 1, AU_RATE, 1,
                           120000) < 0) {
        snd_pcm_close(g_pcm);
        g_pcm = NULL;
        return -1;
    }
    g_live = live;
    atomic_store(&g_run, 1);
    if (pthread_create(&g_thread, NULL, pump, NULL) != 0) {
        snd_pcm_close(g_pcm);
        g_pcm = NULL;
        return -1;
    }
    return 0;
}

void audev_stop(void) {
    if (!g_pcm) return;
    atomic_store(&g_run, 0);
    pthread_join(g_thread, NULL);
    snd_pcm_close(g_pcm);
    g_pcm = NULL;
}
const char *audev_name(void) { return "ALSA (default device)"; }
