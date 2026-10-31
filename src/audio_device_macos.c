/* CoreAudio playback backend (AudioQueue, plain C API): the render
 * callback pulls the live synth and converts to 16-bit mono PCM. */
#include "engine.h"
#include "platform.h"
#include <AudioToolbox/AudioToolbox.h>

#define NBUF 3
#define BUF_FRAMES 1024

static AudioLive *g_live;
static AudioQueueRef g_queue;
static AudioQueueBufferRef g_buffers[NBUF];

static void fill(AudioQueueRef q, AudioQueueBufferRef b) {
    (void)q;
    int16_t *dst = (int16_t *)b->mAudioData;
    float tmp[BUF_FRAMES];
    au_live_render(g_live, tmp, BUF_FRAMES);
    for (int i = 0; i < BUF_FRAMES; i++) {
        float v = tmp[i];
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        dst[i] = (int16_t)(v * 30000);
    }
    b->mAudioDataByteSize = BUF_FRAMES * 2;
    AudioQueueEnqueueBuffer(g_queue, b, 0, NULL);
}

static void callback(void *ud, AudioQueueRef q, AudioQueueBufferRef b) {
    (void)ud; (void)q;
    fill(q, b);
}

int audev_start(AudioLive *live) {
    g_live = live;
    AudioStreamBasicDescription fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.mSampleRate = AU_RATE;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger |
                       kLinearPCMFormatFlagIsPacked;
    fmt.mBytesPerPacket = 2;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = 2;
    fmt.mChannelsPerFrame = 1;
    fmt.mBitsPerChannel = 16;
    if (AudioQueueNewOutput(&fmt, callback, NULL, NULL, NULL, 0, &g_queue) != 0)
        return -1;
    for (int i = 0; i < NBUF; i++) {
        if (AudioQueueAllocateBuffer(g_queue, BUF_FRAMES * 2, &g_buffers[i]) != 0)
            return -1;
        fill(g_queue, g_buffers[i]);
    }
    if (AudioQueueStart(g_queue, NULL) != 0) return -1;
    return 0;
}

void audev_stop(void) {
    if (!g_queue) return;
    AudioQueueStop(g_queue, true);
    AudioQueueDispose(g_queue, true);
    g_queue = NULL;
}
const char *audev_name(void) { return "CoreAudio (AudioQueue)"; }
