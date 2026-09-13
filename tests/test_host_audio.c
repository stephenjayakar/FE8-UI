#include "host_audio.h"

#include <mgba/core/core.h>
#include <mgba-util/audio-buffer.h>
#include <assert.h>
#include <stdio.h>

static struct mAudioBuffer source;
static unsigned rate = 65536;
static size_t configured_threshold;
static int failures;

static void set_size(struct mCore *core, size_t size) {
    (void)core;
    configured_threshold = size;
}

static unsigned get_rate(const struct mCore *core) {
    (void)core;
    return rate;
}

static struct mAudioBuffer *get_buffer(struct mCore *core) {
    (void)core;
    return &source;
}

/* Keep the real mGBA ring-buffer implementation and real SDL conversion.
 * Only the core callbacks are synthetic: this test needs no ROM or speakers. */
static void produce(size_t count, int16_t amplitude) {
    int16_t samples[2048 * 2];
    assert(count <= 2048);
    for (size_t i = 0; i < count; ++i) {
        int16_t value = (i & 1) ? amplitude : -amplitude;
        samples[i * 2] = value;
        samples[i * 2 + 1] = value;
    }
    /* A full core ring rejects newer samples, just as during muted speed-up. */
    mAudioBufferWrite(&source, samples, count);
}

static void check(const char *name, int pass, unsigned long long observed) {
    printf("%s: %s (observed=%llu)\n", pass ? "PASS" : "FAIL", name, observed);
    if (!pass) ++failures;
}

int main(void) {
    struct mCore core = {
        .setAudioBufferSize = set_size,
        .audioSampleRate = get_rate,
        .getAudioBuffer = get_buffer,
    };
    Fe8HostAudio audio;
    mAudioBufferInit(&source, 16384, 2);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_AUDIO) || !fe8_host_audio_init(&audio, &core)) {
        fprintf(stderr, "setup failed: %s\n", SDL_GetError());
        mAudioBufferDeinit(&source);
        SDL_Quit();
        return 2;
    }
    printf("Harness: real SDL2 conversion and dummy output; "
        "real mGBA PCM ring with synthetic core callbacks.\n");
    printf("Sample rate=%u, ring capacity=%zu, configured threshold=%zu\n",
        rate, mAudioBufferCapacity(&source), configured_threshold);
    fe8_host_audio_set_enabled(&audio, 1);
    fe8_host_audio_set_enabled(&audio, 0);
    produce(1024, 24000);
    fe8_host_audio_drain(&audio);
    check("muted drain discards generated PCM",
        mAudioBufferAvailable(&source) == 0, mAudioBufferAvailable(&source));

    mAudioBufferClear(&source);
    produce(1536, 24000);
    fe8_host_audio_set_enabled(&audio, 1);
    check("resume discards muted-epoch PCM",
        mAudioBufferAvailable(&source) == 0, mAudioBufferAvailable(&source));
    produce(1024, 1000);
    audio.peak_sample = 0;
    fe8_host_audio_drain(&audio);
    check("only fresh samples reach the converter after resume", audio.peak_sample == 1000,
          audio.peak_sample);

    audio.peak_sample = 0;
    produce(1024, 1000);
    fe8_host_audio_drain(&audio);
    check("next normal drain has fresh input without resetting the core",
          audio.peak_sample == 1000, audio.peak_sample);

    produce(128, 1000);
    fe8_host_audio_set_enabled(&audio, 1);
    check("repeated enabled notification preserves live PCM",
        mAudioBufferAvailable(&source) == 128, mAudioBufferAvailable(&source));
    mAudioBufferClear(&source);

    unsigned stale_cycles = 0, muted_backlogs = 0;
    size_t maximum_backlog = 0;
    for (unsigned cycle = 0; cycle < 1000; ++cycle) {
        fe8_host_audio_set_enabled(&audio, 0);
        for (unsigned batch = 0; batch < 20; ++batch) {
            produce(1100, 24000);
            fe8_host_audio_drain(&audio);
            size_t retained = mAudioBufferAvailable(&source);
            if (retained)
                ++muted_backlogs;
            if (retained > maximum_backlog)
                maximum_backlog = retained;
        }
        fe8_host_audio_set_enabled(&audio, 1);
        produce(1024, 1000);
        audio.peak_sample = 0;
        fe8_host_audio_drain(&audio);
        if (audio.peak_sample > 1000)
            ++stale_cycles;
    }
    check("1000 speed-up/resume cycles never forward stale PCM", stale_cycles == 0, stale_cycles);
    check("muted PCM stays empty after each serviced batch", muted_backlogs == 0, muted_backlogs);
    printf("Maximum retained muted frames after drain=%zu\n", maximum_backlog);

    fe8_host_audio_set_enabled(&audio, 0);
    rate = 32768;
    produce(512, 24000);
    fe8_host_audio_set_enabled(&audio, 1);
    fe8_host_audio_drain(&audio);
    produce(1024, 1000);
    audio.peak_sample = 0;
    fe8_host_audio_drain(&audio);
    check("sample-rate change rebuilds converter without stale PCM",
          audio.core_rate == rate && audio.peak_sample == 1000,
          audio.core_rate);
    printf("TOTAL: %d failed checks\n", failures);
    fe8_host_audio_deinit(&audio);
    mAudioBufferDeinit(&source);
    SDL_Quit();
    return failures ? 1 : 0;
}
