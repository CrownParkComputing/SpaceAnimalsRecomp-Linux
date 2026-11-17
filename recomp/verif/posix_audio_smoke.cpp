#include "../runtime/os/audio_out.cpp"

#include <cassert>

namespace hos {
void logf(const char*, const char*, ...) {}
}

int main() {
    SDL_AudioSpec format{SDL_AUDIO_F32, 2, 48000};
    SDL_AudioStream* stream = SDL_CreateAudioStream(&format, &format);
    assert(stream);
    uint64_t mixed = 0;
    static uint64_t* count = &mixed;
    hos::audio_out::g_mix = [](float* data, uint32_t frames) {
        assert(frames <= 4096);
        *count += frames;
        std::fill(data, data + frames * 2, 0.125f);
    };
    for (const int bytes : {0, -1, 8, 4096 * 8, 4097 * 8, 12000 * 8, 9}) {
        const uint64_t before = mixed;
        hos::audio_out::sdl_callback(nullptr, stream, bytes, bytes);
        const int frames = bytes <= 0 ? 0 : (bytes + 7) / 8;
        assert(mixed - before == (uint64_t)frames);
        assert(SDL_GetAudioStreamQueued(stream) == frames * 8);
        assert(SDL_ClearAudioStream(stream));
    }
    SDL_DestroyAudioStream(stream);
    puts("SDL audio checks passed (large requests, bounded mixing, exact frame counts)");
}
