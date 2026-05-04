// Minimal SDL2 audio stub for native builds (no actual audio output)
#pragma once

#include <stdint.h>
#include <string.h>

typedef uint8_t  Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;
typedef uint32_t SDL_AudioDeviceID;

#define AUDIO_U8   0x0008
#define AUDIO_S16  0x8010
#define SDL_AUDIO_ALLOW_FORMAT_CHANGE 0x01

typedef void (*SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);

typedef struct {
    int freq;
    Uint16 format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint32 size;
    SDL_AudioCallback callback;
    void *userdata;
} SDL_AudioSpec;

#define SDL_zero(x) memset(&(x), 0, sizeof(x))

inline SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture,
    const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed_changes) {
    if (obtained) *obtained = *desired;
    return 1; // fake device ID
}

inline void SDL_CloseAudioDevice(SDL_AudioDeviceID dev) {}
inline void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on) {}
inline const char *SDL_GetError() { return "SDL stubbed out"; }
