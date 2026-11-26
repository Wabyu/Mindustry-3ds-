// Platform services implemented by main_3ds.cpp and by the host test harness.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace platform {

// libctru key bits
enum Key : uint32_t {
    KA = 1u << 0, KB = 1u << 1, KSelect = 1u << 2, KStart = 1u << 3,
    KRight = 1u << 4, KLeft = 1u << 5, KUp = 1u << 6, KDown = 1u << 7,
    KR = 1u << 8, KL = 1u << 9, KX = 1u << 10, KY = 1u << 11,
};

struct Input {
    uint32_t down = 0, held = 0, up = 0;
    float stickX = 0, stickY = 0;  // circle pad, -1..1, y up
    bool touching = false, touchStart = false, touchEnd = false;
    int touchX = 0, touchY = 0;
};

bool readFile(const std::string& path, std::vector<uint8_t>& out);
bool writeFileAtomic(const std::string& path, const std::vector<uint8_t>& data);
std::string assetPath(const std::string& rel);  // romfs:/rel on 3DS
std::string dataPath(const std::string& rel);   // sdmc:/3ds/mindustry-3ds/rel on 3DS
std::string languageCode();                    // "fr", "en", ...
uint64_t millis();
uint64_t micros();  // high resolution timer (svcGetSystemTick on the 3DS)

}  // namespace platform
