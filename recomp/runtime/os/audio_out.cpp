// Sortie audio du HLE nn::audio (hle_audio.cpp) : WASAPI en mode partagé sous Windows, SDL3 sous
// Linux. Les deux demandent 48 kHz, stéréo, flottant 32 bits ; le périphérique convertit si besoin.
//
// Le fil de rendu demande au mixeur du HLE autant de trames que le tampon du périphérique en accepte :
// l'horloge de la carte son rythme ainsi la consommation des flux du jeu (pas de dérive entre deux
// horloges). Périphérique absent ou perdu : nouvel essai toutes les secondes, et le HLE revient à
// son horloge virtuelle, pour que le jeu n'attende jamais une carte son.
//
// Réglages : RECOMP_NO_AUDIO=1 (pas de sortie), RECOMP_VOLUME=0..100 (gain de sortie, 100 par défaut),
// RECOMP_AUDIO_WAV=fichier.wav (enregistre le mixage, avant le volume : vérification sans haut-parleurs).
#include "posix/win32.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propidl.h>
#else
#include <SDL3/SDL.h>
#endif

#include "audio_out.h"
#include "hos.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

namespace hos {
namespace audio_out {
namespace {

std::atomic<bool> g_active{false};
MixFn g_mix = nullptr;

// ---------------------------------------------------------------- enregistrement WAV (diagnostic)
struct Wav {
    FILE* f = nullptr;
    uint64_t frames = 0, last_hdr = 0;
    void open(const char* path) {
        f = fopen(path, "wb");
        if (!f) {
            HLOG("audio", "RECOMP_AUDIO_WAV: cannot create %s", path);
            return;
        }
        header();
        HLOG("audio", "mix saved to %s (48 kHz, stereo, float)", path);
    }
    void header() {   // tailles réécrites régulièrement : fichier lisible même si le processus est tué
        const uint32_t data = (uint32_t)std::min<uint64_t>(frames * 8, 0xFFFFFF00u);
        struct {
            char riff[4];
            uint32_t size;
            char wave[4], fmt[4];
            uint32_t fmtsize;
            uint16_t tag, ch;
            uint32_t rate, bps;
            uint16_t align, bits;
            char dat[4];
            uint32_t datasize;
        } h = {{'R', 'I', 'F', 'F'}, 36 + data, {'W', 'A', 'V', 'E'}, {'f', 'm', 't', ' '}, 16, 3 /* IEEE float */, 2,
               48000, 48000 * 8, 8, 32, {'d', 'a', 't', 'a'}, data};
        fseek(f, 0, SEEK_SET);
        fwrite(&h, sizeof h, 1, f);
        fseek(f, 0, SEEK_END);
    }
    void write(const float* p, uint32_t n) {
        if (!f) return;
        fwrite(p, 8, n, f);
        frames += n;
        if (frames - last_hdr >= 24000) {   // toutes les 0,5 s
            last_hdr = frames;
            header();
            fflush(f);
        }
    }
};

#ifndef _WIN32

// ---------------------------------------------------------------- périphérique (SDL3)
SDL_AudioDeviceID g_dev = 0;
SDL_AudioStream* g_stream = nullptr;
Wav g_wav;
float g_vol = 1.0f;
constexpr int kMixFrames = 4096;
std::array<float, kMixFrames * 2> g_buf{};
std::atomic<uint64_t> g_output_frames{0};
std::atomic<int> g_max_request{0};

// Le mélange est tiré dans la rappel de SDL : le périphérique son horloge.
void sdl_callback(void* user, SDL_AudioStream* stream, int additional_amount, int total_amount) {
    (void)user;
    (void)total_amount;
    if (additional_amount <= 0) return;
    constexpr int frame_bytes = 2 * sizeof(float);
    int remaining = additional_amount / frame_bytes + (additional_amount % frame_bytes != 0);
    int maximum = g_max_request.load(std::memory_order_relaxed);
    while (remaining > maximum && !g_max_request.compare_exchange_weak(maximum, remaining, std::memory_order_relaxed)) {}
    // SDL requests can exceed one block after a scheduling stall. Never write
    // the entire request into the fixed-size scratch buffer.
    while (remaining > 0) {
        const int frames = std::min(remaining, kMixFrames);
        float* out = g_buf.data();
        if (g_mix) g_mix(out, (uint32_t)frames);
        else memset(out, 0, (size_t)frames * frame_bytes);
        g_wav.write(out, (uint32_t)frames);
        if (g_vol != 1.0f)
            for (int i = 0; i < 2 * frames; i++) out[i] *= g_vol;
        if (!SDL_PutAudioStreamData(stream, out, frames * frame_bytes)) return;
        g_output_frames.fetch_add(frames, std::memory_order_relaxed);
        remaining -= frames;
    }
}

bool open_sdl(bool quiet) {
    SDL_AudioSpec want{};
    want.format = SDL_AUDIO_F32;
    want.channels = 2;
    want.freq = 48000;
    g_dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want);
    if (!g_dev) {
        if (!quiet) HLOG("audio", "sortie audio : %s", SDL_GetError());
        return false;
    }
    g_stream = SDL_CreateAudioStream(&want, &want);
    if (g_stream && !SDL_SetAudioStreamGetCallback(g_stream, sdl_callback, nullptr)) {
        SDL_DestroyAudioStream(g_stream);
        g_stream = nullptr;
    }
    if (!g_stream) {
        if (!quiet) HLOG("audio", "flux audio : %s", SDL_GetError());
        SDL_CloseAudioDevice(g_dev);
        g_dev = 0;
        return false;
    }
    if (!SDL_BindAudioStream(g_dev, g_stream)) {
        if (!quiet) HLOG("audio", "liaison du flux : %s", SDL_GetError());
        SDL_DestroyAudioStream(g_stream);
        SDL_CloseAudioDevice(g_dev);
        g_stream = nullptr;
        g_dev = 0;
        return false;
    }
    SDL_AudioSpec got{};
    if (SDL_GetAudioDeviceFormat(g_dev, &got, nullptr)) {
        HLOG("audio", "sortie audio ouverte : 48 kHz stéréo flottant → périphérique %d Hz, %d canaux", got.freq,
             got.channels);
    } else {
        HLOG("audio", "sortie audio ouverte : 48 kHz stéréo flottant");
    }
    g_active = true;
    SDL_ResumeAudioDevice(g_dev);
    return true;
}

void render_thread() {
    if (getenv("RECOMP_NO_AUDIO")) return;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        HLOG("audio", "sortie audio : SDL_Init a échoué : %s", SDL_GetError());
        return;
    }
    const char* vol_env = getenv("RECOMP_VOLUME");
    g_vol = vol_env ? std::clamp(atoi(vol_env), 0, 100) / 100.0f : 1.0f;
    if (const char* p = getenv("RECOMP_AUDIO_WAV")) g_wav.open(p);
    const bool log = getenv("RECOMP_AUDIO_LOG") != nullptr;
    auto last_log = std::chrono::steady_clock::now();
    uint64_t last_frames = 0;
    int tries = 0;
    for (;;) {
        if (!g_dev) {
            if (!open_sdl(tries++ > 0)) {
                Sleep(1000);
                continue;
            }
            tries = 0;
        }
        Sleep(50);
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - last_log).count();
        if (log && elapsed >= 10.0) {
            const uint64_t frames = g_output_frames.load(std::memory_order_relaxed);
            HLOG("audio", "SDL output: %.0f frames/s (target 48000), largest request %d frames, %d bytes queued",
                 (frames - last_frames) / elapsed, g_max_request.exchange(0), SDL_GetAudioStreamQueued(g_stream));
            last_log = now;
            last_frames = frames;
        }
        // périphérique débranché : le flux n'a plus de périphérique, on rouvre
        if (!SDL_GetAudioStreamDevice(g_stream)) {
            HLOG("audio", "sortie audio perdue : réouverture");
            if (g_stream) SDL_DestroyAudioStream(g_stream);
            if (g_dev) SDL_CloseAudioDevice(g_dev);
            g_stream = nullptr;
            g_dev = 0;
            g_active = false;
        }
    }
}

#else   // ---------------------------------------------------------------- périphérique (Windows, WASAPI)

// identifiants COM (définis ici : pas de dépendance à uuid.lib)
const GUID kCLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const GUID kIID_IMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const GUID kIID_IAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const GUID kIID_IAudioRenderClient = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
const GUID kSUBTYPE_IEEE_FLOAT = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

// Nom du périphérique (PKEY_Device_FriendlyName) pour le journal : on sait ainsi où sort le son.
std::string device_name(IMMDevice* dev) {
    static const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
    std::string out = "?";
    IPropertyStore* ps = nullptr;
    if (FAILED(dev->OpenPropertyStore(STGM_READ, &ps)) || !ps) return out;
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(ps->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, nullptr, 0, nullptr, nullptr);
        if (n > 1) {
            out.assign((size_t)n - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, &out[0], n, nullptr, nullptr);
        }
    }
    PropVariantClear(&v);
    ps->Release();
    return out;
}

struct Dev {
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    HANDLE ev = nullptr;
    UINT32 frames = 0;

    void close() {
        g_active = false;
        if (client) client->Stop();
        if (render) render->Release();
        if (client) client->Release();
        if (dev) dev->Release();
        if (en) en->Release();
        if (ev) CloseHandle(ev);
        *this = Dev{};
    }
    bool fail(const char* what, HRESULT hr, bool quiet) {
        if (!quiet) HLOG("audio", "audio output: %s (0x%08lx)", what, (unsigned long)hr);
        close();
        return false;
    }
    bool open(bool quiet) {
        HRESULT hr = CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator, (void**)&en);
        if (FAILED(hr)) return fail("device enumerator unavailable", hr, quiet);
        hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
        if (FAILED(hr)) return fail("no output device", hr, quiet);
        hr = dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&client);
        if (FAILED(hr)) return fail("audio client activation refused", hr, quiet);
        WAVEFORMATEX* mixf = nullptr;
        uint32_t dev_rate = 0, dev_ch = 0;
        if (SUCCEEDED(client->GetMixFormat(&mixf)) && mixf) {
            dev_rate = mixf->nSamplesPerSec, dev_ch = mixf->nChannels;
            CoTaskMemFree(mixf);
        }
        WAVEFORMATEXTENSIBLE wf{};
        wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wf.Format.nChannels = 2;
        wf.Format.nSamplesPerSec = 48000;
        wf.Format.wBitsPerSample = 32;
        wf.Format.nBlockAlign = 8;
        wf.Format.nAvgBytesPerSec = 48000 * 8;
        wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        wf.Samples.wValidBitsPerSample = 32;
        wf.dwChannelMask = 0x3;   // avant gauche + avant droite
        wf.SubFormat = kSUBTYPE_IEEE_FLOAT;
        // 40 ms de tampon côté Windows (période du moteur audio ≈ 10 ms)
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                    AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                400000, 0, &wf.Format, nullptr);
        if (FAILED(hr)) return fail("48 kHz stereo float format refused", hr, quiet);
        ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!ev || FAILED(client->SetEventHandle(ev))) return fail("render event", E_FAIL, quiet);
        if (FAILED(client->GetBufferSize(&frames)) || !frames) return fail("buffer size", E_FAIL, quiet);
        hr = client->GetService(kIID_IAudioRenderClient, (void**)&render);
        if (FAILED(hr)) return fail("service de rendu", hr, quiet);
        BYTE* p = nullptr;
        if (SUCCEEDED(render->GetBuffer(frames, &p))) render->ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT);
        hr = client->Start();
        if (FAILED(hr)) return fail("start", hr, quiet);
        HLOG("audio", "Windows audio output opened on '%s': 48 kHz stereo float → device %u Hz, %u channels (buffer of %u frames)",
             device_name(dev).c_str(), dev_rate, dev_ch, frames);
        g_active = true;
        return true;
    }
};

void render_thread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    static const char* vol_env = getenv("RECOMP_VOLUME");
    const float vol = vol_env ? std::clamp(atoi(vol_env), 0, 100) / 100.0f : 1.0f;
    Wav wav;
    if (const char* p = getenv("RECOMP_AUDIO_WAV")) wav.open(p);
    Dev d;
    int tries = 0;
    for (;;) {
        if (!d.client) {
            if (!d.open(tries++ > 0)) {   // premier échec journalisé, les nouveaux essais en silence
                Sleep(1000);
                continue;
            }
            tries = 0;
        }
        WaitForSingleObject(d.ev, 100);
        UINT32 pad = 0;
        HRESULT hr = d.client->GetCurrentPadding(&pad);
        if (FAILED(hr)) {   // périphérique débranché ou changé : réouverture (horloge virtuelle en attendant)
            HLOG("audio", "audio output lost (0x%08lx): reopening", (unsigned long)hr);
            d.close();
            continue;
        }
        const UINT32 n = d.frames - std::min(pad, d.frames);
        if (!n) continue;
        BYTE* p = nullptr;
        if (FAILED(d.render->GetBuffer(n, &p)) || !p) {
            d.close();
            continue;
        }
        float* out = (float*)p;
        g_mix(out, n);
        wav.write(out, n);
        if (vol != 1.0f)
            for (UINT32 i = 0; i < 2 * n; i++) out[i] *= vol;
        d.render->ReleaseBuffer(n, 0);
    }
}

#endif  // _WIN32

}  // namespace

void start(MixFn mix) {
    if (getenv("RECOMP_NO_AUDIO")) {
        HLOG("audio", "RECOMP_NO_AUDIO: no audio output (streams consumed by the virtual clock)");
        return;
    }
    g_mix = mix;
    std::thread(render_thread).detach();
}
bool active() { return g_active.load(std::memory_order_relaxed); }

}  // namespace audio_out
}  // namespace hos
