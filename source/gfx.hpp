// Minimal drawing interface. Implemented by citro2d on the 3DS (gfx_3ds.cpp) and by a
// command recorder on the host (tests/gfx_host.cpp) used to produce screenshots.
#pragma once
#include <cstdint>

namespace gfx {

enum Screen { Top = 0, Bottom = 1 };
constexpr int TopW = 400, BottomW = 320, H = 240;

// colors are 0xRRGGBBAA
void begin(Screen s, uint32_t clear);
// draw sprite `id` centred at (x, y) in screen pixels; rotation in degrees, counter-clockwise
void sprite(int id, float x, float y, float rot = 0, float sx = 1, float sy = 1, uint32_t color = 0xffffffff, float blend = 0);
void rect(float x, float y, float w, float h, uint32_t color);
void line(float x0, float y0, float x1, float y1, float width, uint32_t color);
void text(float x, float y, float scale, uint32_t color, const char* str, int align = 0);  // align: 0 left, 1 centre, 2 right
float textWidth(const char* str, float scale);
int spriteW(int id);
int spriteH(int id);

inline void rectLine(float x, float y, float w, float h, float t, uint32_t c) {
    rect(x, y, w, t, c); rect(x, y + h - t, w, t, c); rect(x, y, t, h, c); rect(x + w - t, y, t, h, c);
}

}  // namespace gfx
