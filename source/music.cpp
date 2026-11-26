// The worker is the sole owner of the file and NDSP channel.
#include "music.hpp"
#include <3ds.h>
#include <atomic>
#include <algorithm>
#include <cstdio>

namespace music {
namespace {
constexpr unsigned frames = 8192, bytes = frames * 4, buffers = 3;
constexpr int channel = 0;
std::atomic<Scene> requested{Scene::Silent};
std::atomic<unsigned> volume{75}, suspended{0};
std::atomic<bool> running{false};
aptHookCookie hook{};
Thread thread = nullptr;
unsigned char* samples = nullptr;

void lifecycle(APT_HookType event, void*) {
    if (event == APTHOOK_ONSLEEP) suspended.fetch_or(1);
    if (event == APTHOOK_ONWAKEUP) suspended.fetch_and(~1u);
    if (event == APTHOOK_ONSUSPEND) suspended.fetch_or(2);
    if (event == APTHOOK_ONRESTORE) suspended.fetch_and(~2u);
}

void stream(void*) {
    ndspWaveBuf queue[buffers]{};
    FILE* file = nullptr;
    Scene current = Scene::Silent;
    bool ended = false;
    unsigned nextBuffer = 0;
    unsigned previousVolume = 101;
    u64 nextPlay = 0;
    ndspChnReset(channel);
    ndspChnSetFormat(channel, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetRate(channel, 22050);
    ndspChnSetInterp(channel, NDSP_INTERP_LINEAR);
    while (running.load()) {
        if (suspended.load()) { svcSleepThread(20000000); continue; }
        Scene scene = requested.load();
        if (scene != current) {
            ndspChnWaveBufClear(channel);
            for (auto& w : queue) w = {};
            if (file) std::fclose(file);
            file = nullptr;
            ended = false;
            nextPlay = 0;
            nextBuffer = 0;
            current = scene;
        }
        unsigned level = volume.load();
        if (level != previousVolume) {
            float mix[12]{};
            mix[0] = mix[1] = level / 100.f;
            ndspChnSetMix(channel, mix);
            previousVolume = level;
        }
        if (!file && current != Scene::Silent && osGetTime() >= nextPlay) {
            const char* path = current == Scene::Menu ? "romfs:/music/menu.pcm" :
                               current == Scene::Danger ? "romfs:/music/game2.pcm" : "romfs:/music/game1.pcm";
            file = std::fopen(path, "rb");
            if (!file) nextPlay = osGetTime() + 30000;
        }
        if (file && !ended) {
            for (unsigned filled = 0; filled < buffers; ++filled) {
                unsigned i = nextBuffer;
                auto& w = queue[i];
                if (w.status != NDSP_WBUF_FREE && w.status != NDSP_WBUF_DONE) break;
                auto* ptr = samples + bytes * i;
                size_t count = std::fread(ptr, 4, frames, file);
                if (count < frames && current == Scene::Menu && !std::ferror(file)) {
                    std::rewind(file);
                    count += std::fread(ptr + 4 * count, 4, frames - count, file);
                }
                if (count) {
                    w.data_vaddr = ptr;
                    w.nsamples = count;
                    DSP_FlushDataCache(ptr, count * 4);
                    ndspChnWaveBufAdd(channel, &w);
                    nextBuffer = (i + 1) % buffers;
                }
                if (count < frames) { ended = true; break; }
            }
        }
        if (file && ended && !ndspChnIsPlaying(channel)) {
            std::fclose(file);
            file = nullptr;
            ended = false;
            nextPlay = osGetTime() + 60000;
        }
        svcSleepThread(10000000);
    }
    ndspChnWaveBufClear(channel);
    if (file) std::fclose(file);
}
}

void init() {
    if (R_FAILED(ndspInit())) return;
    samples = static_cast<unsigned char*>(linearAlloc(buffers * bytes));
    if (samples) {
        running = true;
        thread = threadCreate(stream, nullptr, 16384, 0x2f, -2, false);
        if (thread) { aptHook(&hook, lifecycle, nullptr); return; }
        running = false;
        linearFree(samples);
    }
    ndspExit();
}
void update(Scene scene, unsigned level) {
    requested.store(scene);
    volume.store(std::min(100u, level));
}
void shutdown() {
    if (!thread) return;
    running = false;
    threadJoin(thread, U64_MAX);
    threadFree(thread);
    aptUnhook(&hook);
    ndspExit();
    linearFree(samples);
    thread = nullptr;
}
}
