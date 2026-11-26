#pragma once

namespace music {
enum class Scene { Menu, Calm, Danger, Silent };
void init();
void update(Scene scene, unsigned volume);
void shutdown();
}
