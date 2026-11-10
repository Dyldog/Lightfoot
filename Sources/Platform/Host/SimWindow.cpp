#include "Platform/Host/SimWindow.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstdio>

#include "Game/Game.hpp"
#include "Shared/Theme/Theme.hpp"

extern "C" {
#include "font.h"
#include "input.h"
#include "ui.h"
}

namespace lightfoot {

namespace {

constexpr uint32_t kBody = 0x2a2a2e;
constexpr uint32_t kBodyDark = 0x1c1c1f;
constexpr uint32_t kKey = 0x3a3a40;
constexpr uint32_t kKeyDown = 0x6a6a72;
constexpr uint32_t kLabel = 0xd8d8de;
constexpr uint32_t kLabelDim = 0x8a8a92;

struct ButtonSpec {
    int button;
    const char* label;
    const char* key;
};

// Left column: transport and modifiers, top to bottom.
const ButtonSpec kLeftColumn[] = {
    {IN_SHIFT, "SHIFT", "L-Shift"}, {IN_ERASE, "ERASE", "E"},     {IN_PAD_BANK, "BANK", "B"},
    {IN_MINUS, "-", "Left"},        {IN_PLUS, "+", "Right"},      {IN_SEQ_REC, "REC", "R"},
    {IN_STOP, "STOP", "S"},         {IN_PLAY, "PLAY", "Space"},
};
// Right column: the mode keys that Lightfoot does not use yet.
const ButtonSpec kRightColumn[] = {
    {IN_SAMPLE, "SAMPLE", ""}, {IN_SEQ, "SEQ", ""},     {IN_CHOP, "CHOP", ""},
    {IN_MUTE, "MUTE", "M"},    {IN_LOOP, "LOOP", "L"},  {IN_LEVELS, "16 LVL", ""},
    {IN_NOTE_REPEAT, "NOTE RPT", ""}, {IN_TAP_TEMPO, "TAP", "T"},
};

const char* kPadKeys[16] = {"Z", "X", "C", "V", "A", "S", "D", "F", "Q", "W", "E", "R", "1", "2", "3", "4"};

} // namespace

SimWindow::SimWindow(Game& app, int scale) : loop_(app), audio_(app.engine()), scale_(std::max(1, scale)) {
    layout();
}

SimWindow::~SimWindow() {
    if (screenTex_) SDL_DestroyTexture(screenTex_);
    if (panelTex_) SDL_DestroyTexture(panelTex_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void SimWindow::layout() {
    const int s = scale_;
    const int sw = UI_W * s, sh = UI_H * s;
    const int sideW = 96 * s / 2;
    const int softH = 18 * s;
    const int knobH = 46 * s / 2;
    const int padsH = 150 * s / 2;
    winW_ = sideW * 2 + sw;
    winH_ = softH + sh + knobH + padsH;
    screenRect_ = {sideW, softH, sw, sh};

    buttons_.clear();
    knobs_.clear();

    // Soft keys above the screen.
    const int softW = (sw - 2 * 6 * s / 2) / 3;
    for (int i = 0; i < 3; ++i) {
        Rect r{sideW + i * (softW + 6 * s / 2), 4, softW, softH - 8};
        buttons_.push_back({r, IN_B1 + i, -1, std::string("B") + char('1' + i) + "  F" + char('1' + i)});
    }
    // Side columns.
    const int colTop = softH + 6;
    const int colH = (sh - 12) / 8;
    for (int i = 0; i < 8; ++i) {
        Rect l{6, colTop + i * colH, sideW - 12, colH - 6};
        buttons_.push_back({l, kLeftColumn[i].button, -1, std::string(kLeftColumn[i].label) + "\n" + kLeftColumn[i].key});
        Rect r{sideW + sw + 6, colTop + i * colH, sideW - 12, colH - 6};
        buttons_.push_back({r, kRightColumn[i].button, -1, std::string(kRightColumn[i].label) + "\n" + kRightColumn[i].key});
    }
    // Knobs below the screen, plus the encoder at the right.
    const int knobY = softH + sh;
    const int knobW = sw / 4;
    for (int i = 0; i < 3; ++i) {
        Rect r{sideW + i * knobW + 8, knobY + 6, knobW - 16, knobH - 12};
        knobs_.push_back({r, i, 64});
    }
    Rect enc{sideW + 3 * knobW + 8, knobY + 6, knobW - 16, knobH - 12};
    buttons_.push_back({enc, IN_ENCODER, -1, "DATA  Return\nwheel Up/Down"});
    // Pads: 4x4, pad 1 bottom-left.
    const int padsY = knobY + knobH;
    const int padSize = std::min((sw - 5 * 8) / 4, (padsH - 5 * 8) / 4);
    const int padX0 = sideW + (sw - (4 * padSize + 3 * 8)) / 2;
    const int padY0 = padsY + (padsH - (4 * padSize + 3 * 8)) / 2;
    for (int p = 0; p < 16; ++p) {
        const int col = p % 4, row = 3 - p / 4;
        Rect r{padX0 + col * (padSize + 8), padY0 + row * (padSize + 8), padSize, padSize};
        buttons_.push_back({r, -1, p, std::to_string(p + 1) + "  " + kPadKeys[p]});
    }
    panel_.assign(static_cast<size_t>(winW_) * winH_, kBody);
}

// MARK: - Panel painting (software, so the labels use the toolkit font)

void SimWindow::fillRect(Rect r, uint32_t c) {
    r = r.intersect({0, 0, winW_, winH_});
    for (int y = r.y; y < r.bottom(); ++y)
        std::fill(panel_.begin() + static_cast<size_t>(y) * winW_ + r.x, panel_.begin() + static_cast<size_t>(y) * winW_ + r.right(), c);
}

void SimWindow::strokeRect(Rect r, uint32_t c) {
    fillRect({r.x, r.y, r.w, 1}, c);
    fillRect({r.x, r.bottom() - 1, r.w, 1}, c);
    fillRect({r.x, r.y, 1, r.h}, c);
    fillRect({r.right() - 1, r.y, 1, r.h}, c);
}

void SimWindow::drawText(int x, int y, const std::string& s, uint32_t c, int scale) {
    for (char ch : s) {
        int code = static_cast<unsigned char>(ch);
        if (code < 32 || code > 126) code = '?';
        const unsigned char* g = FONT_L[code - 32];
        for (int r = 0; r < FONT_L_H; ++r)
            for (int col = 0; col < FONT_L_W; ++col) {
                if (!(g[r] & (0x80 >> col))) continue;
                fillRect({x + col * scale, y + r * scale, scale, scale}, c);
            }
        x += FONT_L_W * scale;
    }
}

void SimWindow::drawTextCentered(Rect r, const std::string& s, uint32_t c, int scale) {
    // Supports one line break: label on top, key hint below in a dimmer colour.
    const auto nl = s.find('\n');
    const std::string top = nl == std::string::npos ? s : s.substr(0, nl);
    const std::string bottom = nl == std::string::npos ? "" : s.substr(nl + 1);
    const int lineH = FONT_L_H * scale;
    const int totalH = bottom.empty() ? lineH : lineH * 2 + 2;
    int y = r.y + (r.h - totalH) / 2;
    drawText(r.x + (r.w - static_cast<int>(top.size()) * FONT_L_W * scale) / 2, y, top, c, scale);
    if (!bottom.empty())
        drawText(r.x + (r.w - static_cast<int>(bottom.size()) * FONT_L_W * scale) / 2, y + lineH + 2, bottom, kLabelDim, scale);
}

void SimWindow::paintPanel() {
    std::fill(panel_.begin(), panel_.end(), kBody);
    fillRect({0, screenRect_.y, winW_, screenRect_.h}, kBodyDark);
    const int ts = std::max(1, scale_ / 2);
    const auto& leds = loop_.buttonLevels();
    const auto& pads = loop_.padColors();
    for (const PanelButton& b : buttons_) {
        uint32_t fill = kKey;
        if (b.pad >= 0) {
            const uint32_t led = pads[b.pad];
            fill = led ? theme::mix(0x404048, led, 200) : 0x50505a;
            if (pressedPad_ == b.pad) fill = theme::mix(fill, 0xffffff, 90);
        } else {
            if (b.button >= 0 && b.button < 32 && leds[b.button]) fill = leds[b.button] == 2 ? 0xd0a030 : 0x705820;
            if (pressedButton_ >= 0 && pressedButton_ == b.button) fill = kKeyDown;
        }
        fillRect(b.rect, fill);
        strokeRect(b.rect, 0x101012);
        drawTextCentered(b.rect, b.label, kLabel, ts);
    }
    for (const Knob& k : knobs_) {
        fillRect(k.rect, 0x202024);
        strokeRect(k.rect, 0x101012);
        // A horizontal bar for the position, since circles are noisy at this size.
        Rect bar = k.rect.inset(6);
        fillRect(bar, 0x303036);
        const int w = bar.w * k.value / 127;
        fillRect({bar.x, bar.y, w, bar.h}, theme::accent);
        drawTextCentered(k.rect, "K" + std::to_string(k.index + 1) + "  wheel/drag", kLabel, ts);
    }
}

void SimWindow::render() {
    paintPanel();
    SDL_UpdateTexture(panelTex_, nullptr, panel_.data(), winW_ * 4);
    SDL_UpdateTexture(screenTex_, nullptr, loop_.pixels(), UI_W * 4);
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, panelTex_, nullptr, nullptr);
    SDL_Rect dst{screenRect_.x, screenRect_.y, screenRect_.w, screenRect_.h};
    SDL_RenderCopy(renderer_, screenTex_, nullptr, &dst);
    SDL_RenderPresent(renderer_);
}

// MARK: - Input

int SimWindow::keyToButton(int sym) const {
    switch (sym) {
    case SDLK_LSHIFT: case SDLK_RSHIFT: return IN_SHIFT;
    case SDLK_SPACE: return IN_PLAY;
    case SDLK_s: return IN_STOP;
    case SDLK_r: return IN_SEQ_REC;
    case SDLK_e: return IN_ERASE;
    case SDLK_b: return IN_PAD_BANK;
    case SDLK_m: return IN_MUTE;
    case SDLK_l: return IN_LOOP;
    case SDLK_t: return IN_TAP_TEMPO;
    case SDLK_LEFT: return IN_MINUS;
    case SDLK_RIGHT: return IN_PLUS;
    case SDLK_RETURN: case SDLK_KP_ENTER: return IN_ENCODER;
    case SDLK_F1: return IN_B1;
    case SDLK_F2: return IN_B2;
    case SDLK_F3: return IN_B3;
    default: return -1;
    }
}

int SimWindow::keyToPad(int sym) const {
    static const int keys[16] = {SDLK_z, SDLK_x, SDLK_c, SDLK_v, SDLK_a, SDLK_s, SDLK_d, SDLK_f,
                                 SDLK_q, SDLK_w, SDLK_e, SDLK_r, SDLK_1, SDLK_2, SDLK_3, SDLK_4};
    for (int i = 0; i < 16; ++i)
        if (keys[i] == sym) return i;
    return -1;
}

bool SimWindow::handleKey(int sym, bool down) {
    // Pads take priority for letters that double as buttons only when SHIFT-less
    // bindings would collide; here S/E/R are buttons, so pads use their own set.
    const int button = keyToButton(sym);
    if (button >= 0) {
        loop_.push(down ? panelmidi::buttonDown(button) : panelmidi::buttonUp(button));
        return true;
    }
    const int pad = keyToPad(sym);
    if (pad >= 0) {
        loop_.push(down ? panelmidi::padDown(pad, 100) : panelmidi::padUp(pad));
        return true;
    }
    if (down && sym == SDLK_UP) loop_.push(panelmidi::encoder(1));
    if (down && sym == SDLK_DOWN) loop_.push(panelmidi::encoder(-1));
    return sym == SDLK_UP || sym == SDLK_DOWN;
}

void SimWindow::handleMouse(int x, int y, bool down) {
    if (!down) {
        if (pressedButton_ >= 0) loop_.push(panelmidi::buttonUp(pressedButton_));
        if (pressedPad_ >= 0) loop_.push(panelmidi::padUp(pressedPad_));
        pressedButton_ = pressedPad_ = -1;
        return;
    }
    for (const PanelButton& b : buttons_) {
        if (!b.rect.contains(x, y)) continue;
        if (b.pad >= 0) {
            pressedPad_ = b.pad;
            // Velocity from where the pad was struck: top is soft, bottom is hard.
            const int vel = 40 + 87 * (y - b.rect.y) / std::max(1, b.rect.h);
            loop_.push(panelmidi::padDown(b.pad, vel));
        } else {
            pressedButton_ = b.button;
            loop_.push(panelmidi::buttonDown(b.button));
        }
        return;
    }
}

void SimWindow::handleWheel(int x, int y, int dy) {
    for (Knob& k : knobs_) {
        if (!k.rect.contains(x, y)) continue;
        k.value = std::clamp(k.value + dy * 2, 0, 127);
        loop_.push(panelmidi::knob(k.index, k.value));
        return;
    }
    if (dy) loop_.push(panelmidi::encoder(dy > 0 ? 1 : -1));
}

// MARK: - Loop

int SimWindow::run() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    window_ = SDL_CreateWindow("Lightfoot simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW_, winH_,
                               SDL_WINDOW_ALLOW_HIGHDPI);
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!window_ || !renderer_) {
        std::fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer_, winW_, winH_);
    audio_.start();
    screenTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, UI_W, UI_H);
    panelTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, winW_, winH_);

    bool quit = loop_.step(static_cast<int64_t>(SDL_GetTicks64()));
    int64_t nextWake = 0;
    while (!quit) {
        SDL_Event ev;
        bool gotInput = false;
        const int timeout = loop_.wakeMs() > 0 ? loop_.wakeMs() : 50;
        if (SDL_WaitEventTimeout(&ev, timeout)) {
            do {
                switch (ev.type) {
                case SDL_QUIT: quit = true; break;
                case SDL_KEYDOWN:
                    if (ev.key.repeat) break;
                    if (ev.key.keysym.sym == SDLK_ESCAPE) quit = true;
                    gotInput |= handleKey(ev.key.keysym.sym, true);
                    break;
                case SDL_KEYUP: gotInput |= handleKey(ev.key.keysym.sym, false); break;
                case SDL_MOUSEBUTTONDOWN:
                    handleMouse(ev.button.x, ev.button.y, true);
                    gotInput = true;
                    break;
                case SDL_MOUSEBUTTONUP:
                    handleMouse(ev.button.x, ev.button.y, false);
                    gotInput = true;
                    break;
                case SDL_MOUSEWHEEL: {
                    int mx, my;
                    SDL_GetMouseState(&mx, &my);
                    handleWheel(mx, my, ev.wheel.y);
                    gotInput = true;
                    break;
                }
                default: break;
                }
            } while (SDL_PollEvent(&ev));
        }
        if (quit) break;
        const int64_t now = static_cast<int64_t>(SDL_GetTicks64());
        const bool wakeDue = loop_.wakeMs() > 0 && now >= nextWake;
        if (gotInput || loop_.hasPendingInput() || wakeDue) {
            quit = loop_.step(now);
            nextWake = now + loop_.wakeMs();
        }
        render();
    }
    return 0;
}

} // namespace lightfoot
