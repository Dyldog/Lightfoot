#include "Platform/Host/PanelMidi.hpp"

extern "C" {
#include "input.h"
}

namespace lightfoot::panelmidi {

Message buttonDown(int button) {
    return {0x90, INPUT_BUTTON_NOTE[button], 0x7f};
}

Message buttonUp(int button) {
    return {0x80, INPUT_BUTTON_NOTE[button], 0x00};
}

Message padDown(int pad, int velocity) {
    if (velocity < 4) velocity = 4;
    if (velocity > 127) velocity = 127;
    return {0x99, INPUT_PAD_NOTE[pad], static_cast<unsigned char>(velocity)};
}

Message padUp(int pad) {
    return {0x89, INPUT_PAD_NOTE[pad], 0x00};
}

Message padPressure(int pad, int pressure) {
    return {0xa9, INPUT_PAD_NOTE[pad], static_cast<unsigned char>(pressure & 0x7f)};
}

Message encoder(int direction) {
    return {0xb0, 100, static_cast<unsigned char>(direction > 0 ? 0x01 : 0x7f)};
}

Message knob(int index, int value) {
    return {0xb0, static_cast<unsigned char>(16 + index), static_cast<unsigned char>(value & 0x7f)};
}

} // namespace lightfoot::panelmidi
