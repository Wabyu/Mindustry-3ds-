// Host implementation of gfx.hpp and platform.hpp: records draw commands as JSON lines so
// tools/render_shots.py can compose screenshots with the generated sprites.
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#include "gfx.hpp"
#include "platform.hpp"
#include "sprites_gen.hpp"

namespace hostgfx {
std::string out;
std::string romfs = "romfs";
std::string data = "build/host/data";
std::string lang = "fr";
bool recording = false;
}

static void emit(const std::string& s) { if (hostgfx::recording) { hostgfx::out += s; hostgfx::out += '\n'; } }

static std::string esc(const char* s) {
    std::string r;
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') { r += '\\'; r += *s; }
        else if (*s == '\n') r += "\\n";
        else r += *s;
    }
    return r;
}

namespace gfx {
void begin(Screen s, uint32_t clear) {
    char b[96]; std::snprintf(b, sizeof b, "{\"op\":\"begin\",\"screen\":%d,\"color\":%u}", (int)s, clear); emit(b);
}
void sprite(int id, float x, float y, float rot, float sx, float sy, uint32_t color, float blend) {
    if (id < 0 || id >= MDT_SPRITE_COUNT) return;
    char b[200];
    std::snprintf(b, sizeof b, "{\"op\":\"sprite\",\"id\":%d,\"x\":%.2f,\"y\":%.2f,\"rot\":%.2f,\"sx\":%.3f,\"sy\":%.3f,\"color\":%u,\"blend\":%.3f}",
                  id, x, y, rot, sx, sy, color, blend);
    emit(b);
}
void rect(float x, float y, float w, float h, uint32_t c) {
    char b[160]; std::snprintf(b, sizeof b, "{\"op\":\"rect\",\"x\":%.2f,\"y\":%.2f,\"w\":%.2f,\"h\":%.2f,\"color\":%u}", x, y, w, h, c); emit(b);
}
void line(float x0, float y0, float x1, float y1, float width, uint32_t c) {
    char b[200]; std::snprintf(b, sizeof b, "{\"op\":\"line\",\"x0\":%.2f,\"y0\":%.2f,\"x1\":%.2f,\"y1\":%.2f,\"w\":%.2f,\"color\":%u}", x0, y0, x1, y1, width, c); emit(b);
}
void text(float x, float y, float scale, uint32_t c, const char* str, int align) {
    std::string s = "{\"op\":\"text\",\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + ",\"scale\":" + std::to_string(scale) +
                    ",\"color\":" + std::to_string(c) + ",\"align\":" + std::to_string(align) + ",\"text\":\"" + esc(str) + "\"}";
    emit(s);
}
float textWidth(const char* str, float scale) {
    // system font: ~13 px advance per glyph at scale 1 (approximation for layout on host)
    int n = 0; for (const char* p = str; *p; p++) if ((*p & 0xc0) != 0x80) n++;
    return n * 13.f * scale;
}
int spriteW(int id) { return (id >= 0 && id < MDT_SPRITE_COUNT) ? kSpriteRefs[id][2] : 0; }
int spriteH(int id) { return (id >= 0 && id < MDT_SPRITE_COUNT) ? kSpriteRefs[id][3] : 0; }
}  // namespace gfx

namespace platform {
bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    bool ok = n <= 0 || std::fread(out.data(), 1, (size_t)n, f) == (size_t)n;
    std::fclose(f);
    return ok;
}
bool writeFileAtomic(const std::string& path, const std::vector<uint8_t>& data) {
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = std::fclose(f) == 0 && ok;
    return ok && std::rename(tmp.c_str(), path.c_str()) == 0;
}
std::string assetPath(const std::string& rel) { return hostgfx::romfs + "/" + rel; }
std::string dataPath(const std::string& rel) { return hostgfx::data + "/" + rel; }
std::string languageCode() { return hostgfx::lang; }
uint64_t millis() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace platform
