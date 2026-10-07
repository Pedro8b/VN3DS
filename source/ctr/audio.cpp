#include "audio.h"

#include <cstring>

#include "../core/audio_decoder.h"

namespace audio {

namespace {

constexpr int NBUF = 4;
constexpr int FRAMES = 4096;  // per buffer (~93 ms at 44.1 kHz)

struct SlotState {
    int ch = 0;
    LightLock lock;
    bool reqPending = false;
    std::unique_ptr<vn::Stream> reqStream;
    std::string reqName;
    int reqLoops = 1;
    float reqGain = 1.0f;
    int reqFadeMs = 0;

    std::unique_ptr<vn::AudioDecoder> dec;
    int loopsLeft = 0;
    bool decEnded = true;
    int channels = 2;
    float gain = 1.0f;
    bool fading = false;
    u64 fadeStart = 0, fadeLen = 1;
    ndspWaveBuf wb[NBUF];
    int16_t* mem[NBUF] = {nullptr};
    volatile bool playing = false;
};

SlotState g_slots[NUM_SLOTS];
Thread g_thread = nullptr;
LightEvent g_event;
volatile bool g_running = false;
volatile bool g_volDirty = false;
volatile float g_catVol[2] = {0.7f, 1.0f};
bool g_ok = false;
std::string g_lastError;

float categoryVolume(int slot) { return g_catVol[slot < SOUND ? 0 : 1]; }

void applyMix(SlotState& s, float extra = 1.0f) {
    float mix[12] = {0};
    mix[0] = mix[1] = s.gain * categoryVolume(s.ch) * extra;
    ndspChnSetMix(s.ch, mix);
}

void resetBuffers(SlotState& s) {
    ndspChnWaveBufClear(s.ch);
    for (int i = 0; i < NBUF; i++) {
        memset(&s.wb[i], 0, sizeof(ndspWaveBuf));
        s.wb[i].data_vaddr = s.mem[i];
        s.wb[i].status = NDSP_WBUF_FREE;
    }
}

void stopNow(SlotState& s) {
    resetBuffers(s);
    s.dec.reset();
    s.decEnded = true;
    s.fading = false;
}

void startRequest(SlotState& s, std::unique_ptr<vn::Stream> stream, const std::string& name, int loops, float gain) {
    stopNow(s);
    if (!stream) return;
    s.dec = vn::openAudio(std::move(stream), name);
    if (!s.dec) {
        g_lastError = "cannot decode " + name;
        vn::logf(vn::LOG_WARN, "audio: cannot decode %s", name.c_str());
        return;
    }
    s.channels = s.dec->channels();
    s.gain = gain;
    ndspChnReset(s.ch);
    ndspChnSetInterp(s.ch, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(s.ch, (float)s.dec->rate());
    ndspChnSetFormat(s.ch, s.channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    applyMix(s);
    s.loopsLeft = loops == 0 ? 1 : loops;
    s.decEnded = false;
    vn::logf(vn::LOG_DEBUG, "audio[%d]: %s %s %dHz %dch", s.ch, name.c_str(), s.dec->format(), s.dec->rate(),
             s.channels);
}

void fill(SlotState& s) {
    if (!s.dec || s.decEnded) return;
    for (int i = 0; i < NBUF && !s.decEnded; i++) {
        ndspWaveBuf& wb = s.wb[i];
        if (wb.status != NDSP_WBUF_FREE && wb.status != NDSP_WBUF_DONE) continue;
        int16_t* buf = s.mem[i];
        int got = 0, emptyRewinds = 0;
        while (got < FRAMES) {
            int n = s.dec->read(buf + got * s.channels, FRAMES - got);
            if (n > 0) {
                got += n;
                emptyRewinds = 0;
                continue;
            }
            bool again = s.loopsLeft < 0 || s.loopsLeft > 1;
            if (again && emptyRewinds < 2 && s.dec->rewind()) {
                if (s.loopsLeft > 1) s.loopsLeft--;
                emptyRewinds++;
                continue;
            }
            s.decEnded = true;
            break;
        }
        if (got > 0) {
            wb.data_vaddr = buf;
            wb.nsamples = (u32)got;
            wb.looping = false;
            DSP_FlushDataCache(buf, (u32)got * s.channels * sizeof(int16_t));
            ndspChnWaveBufAdd(s.ch, &wb);
        }
    }
}

bool anyQueued(SlotState& s) {
    for (int i = 0; i < NBUF; i++)
        if (s.wb[i].status == NDSP_WBUF_QUEUED || s.wb[i].status == NDSP_WBUF_PLAYING) return true;
    return false;
}

void ndspCallback(void*) { LightEvent_Signal(&g_event); }

void threadMain(void*) {
    while (g_running) {
        bool volDirty = g_volDirty;
        g_volDirty = false;
        for (auto& s : g_slots) {
            bool has = false;
            std::unique_ptr<vn::Stream> st;
            std::string name;
            int loops = 1, fadeMs = 0;
            float gain = 1.0f;
            LightLock_Lock(&s.lock);
            if (s.reqPending) {
                has = true;
                st = std::move(s.reqStream);
                name = s.reqName;
                loops = s.reqLoops;
                gain = s.reqGain;
                fadeMs = s.reqFadeMs;
                s.reqPending = false;
            }
            LightLock_Unlock(&s.lock);
            if (has) {
                if (!st && fadeMs > 0 && s.dec) {
                    // fade out the current stream instead of cutting it
                    s.fading = true;
                    s.fadeStart = svcGetSystemTick();
                    s.fadeLen = (u64)fadeMs * (SYSCLOCK_ARM11 / 1000);
                } else {
                    startRequest(s, std::move(st), name, loops, gain);
                }
            }
            if (s.fading) {
                u64 el = svcGetSystemTick() - s.fadeStart;
                if (el >= s.fadeLen) stopNow(s);
                else applyMix(s, 1.0f - (float)el / (float)s.fadeLen);
            } else if (volDirty && s.dec) {
                applyMix(s);
            }
            fill(s);
            s.playing = (s.dec && !s.decEnded) || anyQueued(s);
            if (!s.playing && s.dec) s.dec.reset();  // release file handles
        }
        LightEvent_Wait(&g_event);
    }
}

}  // namespace

bool init() {
    if (R_FAILED(ndspInit())) {
        g_lastError = "NDSP init failed (missing sdmc:/3ds/dspfirm.cdc? run DSP1 dumper)";
        vn::logf(vn::LOG_ERROR, "%s", g_lastError.c_str());
        return false;
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    LightEvent_Init(&g_event, RESET_ONESHOT);
    for (int i = 0; i < NUM_SLOTS; i++) {
        SlotState& s = g_slots[i];
        s.ch = i;
        LightLock_Init(&s.lock);
        for (int b = 0; b < NBUF; b++) s.mem[b] = (int16_t*)linearAlloc(FRAMES * 2 * sizeof(int16_t));
        resetBuffers(s);
    }
    ndspSetCallback(ndspCallback, nullptr);
    g_running = true;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    // Prefer the syscore (core 1) so decoding never stalls rendering.
    g_thread = threadCreate(threadMain, nullptr, 64 * 1024, prio - 1, 1, false);
    if (!g_thread) g_thread = threadCreate(threadMain, nullptr, 64 * 1024, prio - 1, -2, false);
    g_ok = g_thread != nullptr;
    return g_ok;
}

bool available() { return g_ok; }

void shutdown() {
    if (!g_ok) return;
    g_running = false;
    LightEvent_Signal(&g_event);
    threadJoin(g_thread, U64_MAX);
    threadFree(g_thread);
    for (auto& s : g_slots) {
        ndspChnWaveBufClear(s.ch);
        s.dec.reset();
        for (auto& m : s.mem)
            if (m) linearFree(m);
    }
    ndspSetCallback(nullptr, nullptr);
    ndspExit();
    g_ok = false;
}

void play(int slot, std::unique_ptr<vn::Stream> stream, const std::string& name, int loops, float gain) {
    if (!g_ok || slot < 0 || slot >= NUM_SLOTS) return;
    SlotState& s = g_slots[slot];
    LightLock_Lock(&s.lock);
    s.reqPending = true;
    s.reqStream = std::move(stream);
    s.reqName = name;
    s.reqLoops = loops;
    s.reqGain = gain;
    s.reqFadeMs = 0;
    s.playing = s.reqStream != nullptr;
    LightLock_Unlock(&s.lock);
    LightEvent_Signal(&g_event);
}

void stop(int slot, int fadeMs) {
    if (!g_ok || slot < 0 || slot >= NUM_SLOTS) return;
    SlotState& s = g_slots[slot];
    LightLock_Lock(&s.lock);
    s.reqPending = true;
    s.reqStream.reset();
    s.reqName.clear();
    s.reqFadeMs = fadeMs;
    if (fadeMs <= 0) s.playing = false;
    LightLock_Unlock(&s.lock);
    LightEvent_Signal(&g_event);
}

void stopAll() {
    for (int i = 0; i < NUM_SLOTS; i++) stop(i);
}

bool isPlaying(int slot) { return g_ok && slot >= 0 && slot < NUM_SLOTS && g_slots[slot].playing; }

void setCategoryVolumes(float music, float sound) {
    g_catVol[0] = music < 0 ? 0 : music > 1 ? 1 : music;
    g_catVol[1] = sound < 0 ? 0 : sound > 1 ? 1 : sound;
    g_volDirty = true;
    if (g_ok) LightEvent_Signal(&g_event);
}

std::string lastError() { return g_lastError; }

}  // namespace audio
