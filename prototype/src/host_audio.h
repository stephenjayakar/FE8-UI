#ifndef FE8_HOST_AUDIO_H
#define FE8_HOST_AUDIO_H

#include <SDL.h>
#include <stdint.h>

struct mCore;

typedef struct Fe8HostAudio {
    SDL_AudioDeviceID device;
    SDL_AudioStream *stream;
    SDL_AudioSpec obtained;
    struct mCore *core;
    unsigned core_rate;
    int enabled;
    uint64_t frames_queued;
    uint16_t peak_sample;
} Fe8HostAudio;

int fe8_host_audio_init(Fe8HostAudio *audio, struct mCore *core);
void fe8_host_audio_set_enabled(Fe8HostAudio *audio, int enabled);
void fe8_host_audio_drain(Fe8HostAudio *audio);
void fe8_host_audio_deinit(Fe8HostAudio *audio);

#endif
