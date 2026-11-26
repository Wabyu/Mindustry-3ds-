// Nintendo 3DS entry point and citro2d backend.
#include <3ds.h>
#include <citro2d.h>
#include <sys/stat.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "app.hpp"
#include "gfx.hpp"
#include "sprites_gen.hpp"

// ---------------------------------------------------------------- gfx backend
namespace {
C3D_RenderTarget* targets[2];
C2D_SpriteSheet sheets[MDT_SHEET_COUNT];
C2D_Image images[MDT_SPRITE_COUNT];
C2D_TextBuf textBuf;
int current = 0;

u32 col(uint32_t c) { return C2D_Color32((c >> 24) & 0xff, (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff); }
}

namespace gfx {

void begin(Screen s, uint32_t clear) {
    current = s;
    C2D_TargetClear(targets[s], col(clear));
    C2D_SceneBegin(targets[s]);
}

void sprite(int id, float x, float y, float rot, float sx, float sy, uint32_t color, float blend) {
    if (id < 0 || id >= MDT_SPRITE_COUNT || !images[id].tex) return;
    const C2D_Image& img = images[id];
    float w = kSpriteRefs[id][2] * sx, h = kSpriteRefs[id][3] * sy;
    C2D_DrawParams p;
    p.pos.x = x; p.pos.y = y; p.pos.w = w; p.pos.h = h;
    p.center.x = w / 2; p.center.y = h / 2;
    p.depth = 0.5f;
    p.angle = -rot * (float)M_PI / 180.f;
    if (color == 0xffffffff && blend == 0) {
        C2D_DrawImage(img, &p, nullptr);
    } else {
        C2D_ImageTint t;
        C2D_PlainImageTint(&t, col(color), blend);
        C2D_DrawImage(img, &p, &t);
    }
}

void rect(float x, float y, float w, float h, uint32_t c) { C2D_DrawRectSolid(x, y, 0.5f, w, h, col(c)); }

void line(float x0, float y0, float x1, float y1, float width, uint32_t c) {
    u32 k = col(c);
    C2D_DrawLine(x0, y0, k, x1, y1, k, width, 0.5f);
}

void text(float x, float y, float scale, uint32_t c, const char* str, int align) {
    C2D_Text t;
    C2D_TextParse(&t, textBuf, str);
    C2D_TextOptimize(&t);
    u32 flags = C2D_WithColor | (align == 1 ? C2D_AlignCenter : align == 2 ? C2D_AlignRight : C2D_AlignLeft);
    C2D_DrawText(&t, flags, x, y, 0.5f, scale, scale, col(c));
}

float textWidth(const char* str, float scale) {
    C2D_Text t;
    C2D_TextParse(&t, textBuf, str);
    float w = 0, h = 0;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    return w;
}

int spriteW(int id) { return (id >= 0 && id < MDT_SPRITE_COUNT) ? kSpriteRefs[id][2] : 0; }
int spriteH(int id) { return (id >= 0 && id < MDT_SPRITE_COUNT) ? kSpriteRefs[id][3] : 0; }

}  // namespace gfx

// ---------------------------------------------------------------- platform
namespace platform {

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0) { std::fclose(f); return false; }
    out.resize((size_t)n);
    bool ok = n == 0 || std::fread(out.data(), 1, (size_t)n, f) == (size_t)n;
    std::fclose(f);
    return ok;
}

bool writeFileAtomic(const std::string& path, const std::vector<uint8_t>& data) {
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) { std::remove(tmp.c_str()); return false; }
    std::string bak = path + ".bak";
    std::remove(bak.c_str());
    std::rename(path.c_str(), bak.c_str());  // keep the previous save until the new one is in place
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { std::rename(bak.c_str(), path.c_str()); return false; }
    std::remove(bak.c_str());
    return true;
}

std::string assetPath(const std::string& rel) { return "romfs:/" + rel; }
std::string dataPath(const std::string& rel) { return "sdmc:/3ds/mindustry-3ds/" + rel; }

std::string languageCode() {
    u8 l = CFG_LANGUAGE_EN;
    if (R_FAILED(CFGU_GetSystemLanguage(&l))) return "en";
    switch (l) {
        case CFG_LANGUAGE_FR: return "fr";
        case CFG_LANGUAGE_DE: return "de";
        case CFG_LANGUAGE_IT: return "it";
        case CFG_LANGUAGE_ES: return "es";
        case CFG_LANGUAGE_JP: return "ja";
        default: return "en";
    }
}

uint64_t millis() { return osGetTime(); }

}  // namespace platform

// Homebrew Launcher main-thread stack (the CIA sets StackSize 0x40000 in its RSF)
extern "C" { u32 __stacksize__ = 256 * 1024; }

int main() {
    romfsInit();
    cfguInit();
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(8192);
    C2D_Prepare();
    osSetSpeedupEnable(true);  // 804 MHz + L2 cache on New 3DS; no effect on Old 3DS
    targets[0] = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    targets[1] = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    textBuf = C2D_TextBufNew(4096);
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/mindustry-3ds", 0777);

    bool assetsOk = true;
    for (int s = 0; s < MDT_SHEET_COUNT; s++) {
        char path[64];
        std::snprintf(path, sizeof path, "romfs:/gfx/sheet%d.t3x", s);
        sheets[s] = C2D_SpriteSheetLoad(path);
        if (!sheets[s]) assetsOk = false;
    }
    for (int i = 0; i < MDT_SPRITE_COUNT && assetsOk; i++) images[i] = C2D_SpriteSheetGetImage(sheets[kSpriteRefs[i][0]], kSpriteRefs[i][1]);

    if (!assetsOk) {
        consoleInit(GFX_BOTTOM, nullptr);
        std::printf("romfs:/gfx/sheet*.t3x missing.\nRebuild with `make` (see README).\n\nSTART: exit");
        while (aptMainLoop()) { hidScanInput(); if (hidKeysDown() & KEY_START) break; gspWaitForVBlank(); }
    } else {
        mdt::App app;
        app.init();
        u64 last = osGetTime();
        while (aptMainLoop() && !app.wantsExit()) {
            hidScanInput();
            platform::Input in;
            in.down = hidKeysDown(); in.held = hidKeysHeld(); in.up = hidKeysUp();
            circlePosition cp;
            hidCircleRead(&cp);
            float sx = cp.dx / 156.f, sy = cp.dy / 156.f;
            if (std::fabs(sx) < 0.12f) sx = 0;
            if (std::fabs(sy) < 0.12f) sy = 0;
            in.stickX = std::fmax(-1.f, std::fmin(1.f, sx));
            in.stickY = std::fmax(-1.f, std::fmin(1.f, sy));
            touchPosition tp;
            hidTouchRead(&tp);
            in.touching = (in.held & KEY_TOUCH) != 0;
            in.touchStart = (in.down & KEY_TOUCH) != 0;
            in.touchEnd = (in.up & KEY_TOUCH) != 0;
            in.touchX = tp.px; in.touchY = tp.py;

            u64 now = osGetTime();
            // Time.delta: frame time in 60 Hz ticks, capped like arc's delta provider
            float delta = (now - last) * 60.f / 1000.f;
            last = now;
            if (delta < 0.25f) delta = 0.25f;
            if (delta > 3.f) delta = 3.f;
            app.frame(in, delta);

            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C2D_TextBufClear(textBuf);
            app.draw();
            C3D_FrameEnd(0);
        }
        if (app.screen == mdt::Screen::Game || app.screen == mdt::Screen::Pause) app.saveCurrent();
    }
    C2D_TextBufDelete(textBuf);
    for (int s = 0; s < MDT_SHEET_COUNT; s++) if (sheets[s]) C2D_SpriteSheetFree(sheets[s]);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    cfguExit();
    romfsExit();
    return 0;
}
