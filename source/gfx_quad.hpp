// Sprite placement shared by the citro2d backend and the host recorder, so host
// captures reproduce exactly the parameters sent to C2D_DrawImage.
//
// citro2d (base.c, C2Di_CalcQuad) builds the quad from fabs(pos.w)/fabs(pos.h) but
// offsets it by -center, and flips by swapping UVs when w/h are negative.
// The centre must therefore always be |w|/2, |h|/2, otherwise a mirrored sprite is
// shifted by its own size.
#pragma once
#include <cmath>

namespace gfx {

// Pixel snapping of quarter-turn sprites and of the camera. Always on in the game; the host
// control scenes switch it off to measure what it prevents.
inline bool snapEnabled = true;

struct QuadParams {
    float x, y;    // C2D_DrawParams.pos.x/y (rotation centre)
    float w, h;    // signed size: negative = mirrored
    float cx, cy;  // C2D_DrawParams.center (always positive)
    float angle;   // radians, citro2d convention (clockwise on screen)
};

inline QuadParams spriteQuad(int srcW, int srcH, float x, float y, float rotDeg, float sx, float sy) {
    QuadParams q;
    q.w = srcW * sx;
    q.h = srcH * sy;
    float aw = std::fabs(q.w), ah = std::fabs(q.h);
    q.cx = aw / 2; q.cy = ah / 2;
    float r = std::fmod(rotDeg, 360.f);
    if (r < 0) r += 360.f;
    float quarters = r / 90.f;
    int k = (int)std::lround(quarters);
    if (snapEnabled && std::fabs(quarters - k) < 0.0005f) {
        // Quarter turns: textures use GPU_NEAREST (citro3d default), so the quad edges
        // must land on whole pixels or texels get duplicated/skipped while the camera moves.
        k &= 3;
        float ex = (k & 1) ? ah : aw, ey = (k & 1) ? aw : ah;
        x = std::floor(x - ex / 2 + 0.5f) + ex / 2;
        y = std::floor(y - ey / 2 + 0.5f) + ey / 2;
        r = k * 90.f;
    }
    q.x = x; q.y = y;
    q.angle = r == 0 ? 0.f : -r * 3.14159265358979f / 180.f;
    return q;
}

inline void snapRect(float& x, float& y, float& w, float& h) {
    float x1 = std::floor(x + w + 0.5f), y1 = std::floor(y + h + 0.5f);
    x = std::floor(x + 0.5f); y = std::floor(y + 0.5f);
    w = x1 - x; h = y1 - y;
}

}  // namespace gfx
