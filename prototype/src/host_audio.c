#include "host_audio.h"

#include <mgba/core/core.h>
#include <mgba-util/audio-buffer.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    CORE_AUDIO_CAPACITY = 4096,
    AUDIO_CHUNK_FRAMES = 2048,
    MAX_QUEUED_MILLISECONDS = 120,
};

int fe8_host_audio_init(Fe8HostAudio *audio, struct mCore *core) {
    SDL_AudioSpec desired;
    unsigned core_rate;
    memset(audio, 0, sizeof(*audio));
    memset(&desired, 0, sizeof(desired));
    audio->core = core;
    core->setAudioBufferSize(core, CORE_AUDIO_CAPACITY);
    core_rate = core->audioSampleRate(core);
    desired.freq = 48000;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    audio->device = SDL_OpenAudioDevice(NULL, 0, &desired, &audio->obtained,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!audio->device) {
        fprintf(stderr, "Audio device unavailable: %s\n", SDL_GetError());
        return 0;
    }
    audio->stream = SDL_NewAudioStream(AUDIO_S16SYS, 2, (int)core_rate,
        audio->obtained.format, audio->obtained.channels, audio->obtained.freq);
    if (!audio->stream) {
        fprintf(stderr, "Audio conversion unavailable: %s\n", SDL_GetError());
        SDL_CloseAudioDevice(audio->device);
        audio->device = 0;
        return 0;
    }
    fprintf(stderr, "Audio: core=%u Hz output=%d Hz channels=%u\n",
        core_rate, audio->obtained.freq, audio->obtained.channels);
    return 1;
}

void fe8_host_audio_set_enabled(Fe8HostAudio *audio, int enabled) {
    struct mAudioBuffer *source;
    if (!audio->device)
        return;
    audio->enabled = enabled != 0;
    SDL_PauseAudioDevice(audio->device, audio->enabled ? 0 : 1);
    if (!audio->enabled) {
        SDL_ClearQueuedAudio(audio->device);
        SDL_AudioStreamClear(audio->stream);
        source = audio->core->getAudioBuffer(audio->core);
        mAudioBufferClear(source);
    }
}

void fe8_host_audio_drain(Fe8HostAudio *audio) {
    int16_t input[AUDIO_CHUNK_FRAMES * 2];
    uint8_t output[AUDIO_CHUNK_FRAMES * 8];
    struct mAudioBuffer *source;
    size_t available;
    int bytes;
    uint32_t queued_limit;
    if (!audio->device || !audio->enabled)
        return;
    source = audio->core->getAudioBuffer(audio->core);
    while ((available = mAudioBufferAvailable(source)) != 0) {
        size_t frames = available > AUDIO_CHUNK_FRAMES ? AUDIO_CHUNK_FRAMES : available;
        size_t sample;
        frames = mAudioBufferRead(source, input, frames);
        for (sample = 0; sample < frames * 2; ++sample) {
            uint16_t amplitude = input[sample] < 0 ?
                (uint16_t)(-(int32_t)input[sample]) : (uint16_t)input[sample];
            if (amplitude > audio->peak_sample)
                audio->peak_sample = amplitude;
        }
        audio->frames_queued += frames;
        if (SDL_AudioStreamPut(audio->stream, input,
                (int)(frames * 2 * sizeof(*input))) != 0) {
            fprintf(stderr, "Audio conversion failed: %s\n", SDL_GetError());
            return;
        }
    }
    while ((bytes = SDL_AudioStreamAvailable(audio->stream)) > 0) {
        int chunk = bytes > (int)sizeof(output) ? (int)sizeof(output) : bytes;
        int received = SDL_AudioStreamGet(audio->stream, output, chunk);
        if (received <= 0)
            break;
        if (SDL_QueueAudio(audio->device, output, (uint32_t)received) != 0) {
            fprintf(stderr, "Audio queue failed: %s\n", SDL_GetError());
            break;
        }
    }
    queued_limit = (uint32_t)((uint64_t)audio->obtained.freq *
        audio->obtained.channels * SDL_AUDIO_BITSIZE(audio->obtained.format) / 8 *
        MAX_QUEUED_MILLISECONDS / 1000);
    if (SDL_GetQueuedAudioSize(audio->device) > queued_limit) {
        SDL_ClearQueuedAudio(audio->device);
        SDL_AudioStreamClear(audio->stream);
    }
}

void fe8_host_audio_deinit(Fe8HostAudio *audio) {
    fprintf(stderr, "Audio summary: frames=%llu peak=%u\n",
        (unsigned long long)audio->frames_queued, audio->peak_sample);
    if (audio->device) {
        SDL_PauseAudioDevice(audio->device, 1);
        SDL_ClearQueuedAudio(audio->device);
    }
    if (audio->stream)
        SDL_FreeAudioStream(audio->stream);
    if (audio->device)
        SDL_CloseAudioDevice(audio->device);
    memset(audio, 0, sizeof(*audio));
}
