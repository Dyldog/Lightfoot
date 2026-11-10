#pragma once

#include <array>

namespace lightfoot::panelmidi {

/// A three-byte control-surface message, exactly what the panel MCU sends.
using Message = std::array<unsigned char, 3>;

Message buttonDown(int button);
Message buttonUp(int button);
Message padDown(int pad, int velocity = 100);
Message padUp(int pad);
Message padPressure(int pad, int pressure);
/// One encoder step. Positive is clockwise.
Message encoder(int direction);
/// Absolute knob value 0..127 for K1..K3.
Message knob(int index, int value);

} // namespace lightfoot::panelmidi
