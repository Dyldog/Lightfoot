#pragma once

struct ui;
struct input;

extern "C" {
/// Toolkit-compatible frame callback. `state` is a lightfoot::Game*.
int lightfoot_frame(struct ui* u, const struct input* in, void* state);
}
