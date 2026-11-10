#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Platform/Host/HostLoop.hpp"
#include "Platform/Host/SdlAudio.hpp"
#include "Shared/Drawing/Rect.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace lightfoot {

class Game;

/// SDL2 window showing the 320x240 screen with a clickable panel around it:
/// soft keys above, knobs and pads below, transport on the left. The keyboard
/// mirrors the panel so the whole app is drivable without a mouse.
class SimWindow {
public:
    SimWindow(Game& app, int scale);
    ~SimWindow();

    int run();

private:
    struct PanelButton {
        Rect rect;
        int button;      // IN_* or -1
        int pad;         // 0..15 or -1
        std::string label;
    };
    struct Knob {
        Rect rect;
        int index;
        int value = 64;
    };

    void layout();
    void render();
    void paintPanel();
    void fillRect(Rect r, uint32_t c);
    void strokeRect(Rect r, uint32_t c);
    void drawText(int x, int y, const std::string& s, uint32_t c, int scale);
    void drawTextCentered(Rect r, const std::string& s, uint32_t c, int scale);
    bool handleKey(int sym, bool down);
    void handleMouse(int x, int y, bool down);
    void handleWheel(int x, int y, int dy);
    int keyToButton(int sym) const;
    int keyToPad(int sym) const;

    HostLoop loop_;
    SdlAudio audio_;
    int scale_;
    int winW_ = 0, winH_ = 0;
    Rect screenRect_{};
    std::vector<PanelButton> buttons_;
    std::vector<Knob> knobs_;
    std::vector<uint32_t> panel_;
    int pressedButton_ = -1;
    int pressedPad_ = -1;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* screenTex_ = nullptr;
    SDL_Texture* panelTex_ = nullptr;
};

} // namespace lightfoot
