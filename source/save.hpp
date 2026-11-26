#pragma once
#include <vector>
#include "game.hpp"

namespace mdt {
std::vector<uint8_t> saveGame(const Game& g);
bool loadGame(Game& g, const std::vector<uint8_t>& data);
}
