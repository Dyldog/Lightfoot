#include "Game/GameBridge.hpp"

#include "Game/Game.hpp"

extern "C" int lightfoot_frame(struct ui* u, const struct input* in, void* state) {
    return static_cast<lightfoot::Game*>(state)->frame(u, in);
}
