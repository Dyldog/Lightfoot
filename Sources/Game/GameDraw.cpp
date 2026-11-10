// Everything Lightfoot draws: the night, the course, the runner, the HUD, the
// menus, and the lights on the panel. Game.cpp holds the rules.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Game/Game.hpp"
#include "Shared/Drawing/Canvas.hpp"
#include "Shared/Input/Frame.hpp"

extern "C" {
#include "input.h"
}

namespace lightfoot {

namespace {

constexpr int kRunnerX = 96;
constexpr int kSlotW = 56;
constexpr int kHorizon = 150;
constexpr int kGroundY = 194;
constexpr Rect kScreen{0, 0, Canvas::width, Canvas::height};

uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float hashUnit(uint32_t x) {
    return static_cast<float>(hash32(x) & 0xffff) / 65535.0f;
}

} // namespace

uint32_t Game::nowSeed_() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

// MARK: - Geometry

int Game::rowY(int row) const {
    return 98 + std::clamp(row, 0, 3) * 26; // row 0 is the top row of pads: the highest platform
}

int Game::xForSlot(double slotIndex) const {
    return kRunnerX + static_cast<int>(std::lround((slotIndex - beat_) * kSlotW));
}

double Game::runnerY(double b) const {
    if (fallStart_ >= 0) {
        const double t = std::max(0.0, b - fallStart_);
        return rowY(jumpFrom_) + 8 + t * t * 320;
    }
    if (!landed_ && jumpEnd_ > jumpStart_) {
        const double t = std::clamp((b - jumpStart_) / (jumpEnd_ - jumpStart_), 0.0, 1.0);
        const double from = rowY(jumpFrom_), to = rowY(jumpTo_);
        const double arc = 20 + 6 * std::abs(jumpFrom_ - jumpTo_) + (jumpEnd_ - jumpStart_ > 1.0 ? 14 : 0);
        return from + (to - from) * t - arc * std::sin(t * 3.14159265);
    }
    return rowY(runnerRow_);
}

// MARK: - Frame

void Game::draw(Canvas& canvas, const Frame& in, int64_t nowMs) {
    (void)in;
    switch (state_) {
    case State::Title: drawTitle(canvas, nowMs); break;
    case State::HowTo: drawHowTo(canvas, nowMs); break;
    case State::Scores: drawScores(canvas, nowMs); break;
    case State::Songs: drawSongs(canvas, nowMs); break;
    case State::Browse: break; // the toolkit browser drew itself during update
    case State::Playing: drawPlay(canvas, nowMs); break;
    case State::Paused:
        drawPlay(canvas, nowMs);
        drawPaused(canvas, nowMs);
        break;
    case State::GameOver:
        drawPlay(canvas, nowMs);
        drawGameOver(canvas, nowMs);
        break;
    }
    if (flash_ > 0) canvas.fillAlpha(kScreen, flashColor_, static_cast<int>(flash_ * 160));
    if (!banner_.empty() && nowMs < bannerUntil_) {
        const int64_t left = bannerUntil_ - nowMs;
        const int alpha = static_cast<int>(std::min<int64_t>(255, left * 255 / 400));
        const int scale = banner_.size() <= 2 ? 4 : 2;
        const int w = Canvas::scaledWidth(banner_, scale);
        const int x = (Canvas::width - w) / 2, y = 92 - Canvas::scaledHeight(scale) / 2;
        canvas.fillAlpha({x - 10, y - 6, w + 20, Canvas::scaledHeight(scale) + 12}, 0x000000, alpha / 2);
        canvas.textScaled(x + 2, y + 2, banner_, palette_.accent, scale, alpha / 2);
        canvas.textScaled(x, y, banner_, palette_.white, scale, alpha);
    }
}

// MARK: - The night

void Game::drawSky(Canvas& canvas, int64_t nowMs, double scroll) {
    const Palette& p = palette_;
    // Gradient to the horizon, then the ground.
    for (int y = 0; y < kGroundY; ++y) {
        const int t = y * 255 / kGroundY;
        const uint32_t c = t < 150 ? Palette::mix(p.skyTop, p.skyMid, t * 255 / 150) : Palette::mix(p.skyMid, p.skyHorizon, (t - 150) * 255 / 105);
        canvas.hline(0, Canvas::width - 1, y, c);
    }
    canvas.fill({0, kGroundY, Canvas::width, Canvas::height - kGroundY}, p.ground);

    // Stars: fixed to the sky, twinkling, drifting a little with the scroll.
    for (uint32_t i = 0; i < 70; ++i) {
        const int sx = static_cast<int>(std::fmod(hashUnit(i * 7 + 1) * 400 - scroll * 2.0 + 4000, 400.0)) - 40;
        const int sy = static_cast<int>(hashUnit(i * 7 + 2) * 120);
        const float tw = 0.5f + 0.5f * std::sin(nowMs * 0.002f * (1 + hashUnit(i * 7 + 3) * 2) + i);
        const int a = static_cast<int>(90 + 150 * tw);
        canvas.blend(sx, sy, p.white, a);
        if (hashUnit(i * 7 + 4) > 0.8f) {
            canvas.blend(sx - 1, sy, p.white, a / 3);
            canvas.blend(sx + 1, sy, p.white, a / 3);
            canvas.blend(sx, sy - 1, p.white, a / 3);
            canvas.blend(sx, sy + 1, p.white, a / 3);
        }
    }

    // The sun, low and striped, with its glow on the haze.
    const int sunX = 244, sunY = 104, sunR = 36;
    canvas.glow(sunX, sunY, sunR + 26, p.sun, 110);
    for (int dy = -sunR; dy <= sunR; ++dy) {
        const int y = sunY + dy;
        if (y >= kHorizon) break;
        const int half = static_cast<int>(std::sqrt(static_cast<double>(sunR * sunR - dy * dy)));
        // Stripes thicken towards the bottom, the classic poster sun.
        if (dy > 0 && ((dy + static_cast<int>(nowMs / 90)) % std::max(3, 10 - dy / 5)) < std::min(3, 1 + dy / 14)) continue;
        const uint32_t c = Palette::mix(p.sunCore, p.sun, std::clamp((dy + sunR) * 255 / (2 * sunR), 0, 255));
        canvas.hline(sunX - half, sunX + half, y, c);
    }

    // Mountains: a slow silhouette.
    for (int x = 0; x < Canvas::width; ++x) {
        const double wx = x + scroll * 0.12;
        const double h = 22 * std::sin(wx * 0.021) + 12 * std::sin(wx * 0.053 + 1.3) + 6 * std::sin(wx * 0.11 + 0.4);
        const int top = kHorizon - 22 - static_cast<int>(h);
        canvas.vline(x, top, kHorizon, p.mountain);
    }
    // Far city, then near city: blocks with a few lit windows that blink to the beat.
    for (int layer = 0; layer < 2; ++layer) {
        const double par = layer == 0 ? 0.3 : 0.55;
        const int period = layer == 0 ? 22 : 34;
        const uint32_t body = layer == 0 ? p.cityFar : p.cityNear;
        const double origin = scroll * par;
        const int first = static_cast<int>(std::floor(origin / period)) - 1;
        for (int b = first; b < first + Canvas::width / period + 3; ++b) {
            const uint32_t hb = static_cast<uint32_t>(b * 31 + layer * 977);
            const int w = period - 3 - static_cast<int>(hashUnit(hb + 1) * 6);
            const int h = (layer == 0 ? 16 : 26) + static_cast<int>(hashUnit(hb + 2) * (layer == 0 ? 34 : 46));
            const int x0 = static_cast<int>(std::lround(b * period - origin));
            const int top = kHorizon + (layer == 0 ? 0 : 6) - h;
            canvas.fill({x0, top, w, kGroundY - top}, body);
            if (hashUnit(hb + 3) > 0.5f) canvas.fill({x0 + w / 2 - 1, top - 6, 2, 6}, body); // an aerial
            // Windows
            for (int wy = top + 4; wy < kHorizon - 2; wy += 6)
                for (int wx = x0 + 3; wx < x0 + w - 2; wx += 5) {
                    const uint32_t hw = hash32(hb * 131 + static_cast<uint32_t>(wy * 7 + wx));
                    if ((hw & 7) > 2) continue;
                    const bool blink = ((hw >> 8) & 31) == 0 && ((nowMs / 250 + (hw >> 13)) % 7) == 0;
                    if (blink) continue;
                    canvas.blend(wx, wy, p.window, layer == 0 ? 90 : 170);
                    if (layer == 1) canvas.blend(wx + 1, wy, p.window, 120);
                }
        }
    }
    // Horizon line glow.
    canvas.hline(0, Canvas::width - 1, kHorizon, Palette::mix(p.gridLineBright, p.skyHorizon, 120));
}

void Game::drawWorld(Canvas& canvas, int64_t nowMs) {
    const Palette& p = palette_;
    const double scroll = beat_ * kSlotW;
    const int sx = shake_ > 0 ? static_cast<int>(std::sin(nowMs * 0.09) * shake_ * 4) : 0;
    const int sy = shake_ > 0 ? static_cast<int>(std::cos(nowMs * 0.07) * shake_ * 3) : 0;
    drawSky(canvas, nowMs, scroll);

    // Perspective floor: lines rushing towards the runner, pulsing on the beat.
    const double phase = beatPhase();
    for (int i = 0; i < 9; ++i) {
        const double t = std::fmod(i + phase, 9.0) / 9.0;
        const int y = kGroundY + static_cast<int>(t * t * (Canvas::height - kGroundY));
        const bool bright = i == 0;
        canvas.hline(0, Canvas::width - 1, y + sy, bright ? Palette::mix(p.gridLineBright, p.gridLine, static_cast<int>(phase * 255)) : p.gridLine);
    }
    for (int i = -8; i <= 8; ++i) {
        const int xTop = 160 + i * 6 + sx, xBottom = 160 + i * 46 + sx;
        canvas.line(xTop, kGroundY, xBottom, Canvas::height - 1, p.gridLine);
    }

    // Platforms: a slab per beat at its row, neon edge, glow beneath, and a
    // chevron where a jump is due. Twin cues sit on a gap.
    const int64_t firstSlot = static_cast<int64_t>(std::floor(beat_)) - 2;
    const float pulse = 0.5f + 0.5f * std::sin(nowMs * 0.012f);
    for (int64_t k = firstSlot; k < firstSlot + 8; ++k) {
        const Slot& s = track_.at(std::max<int64_t>(k, 0));
        const int x0 = xForSlot(static_cast<double>(k)) + sx;
        const int x1 = xForSlot(static_cast<double>(k) + 1) - 3 + sx;
        if (x1 <= 0 || x0 >= Canvas::width) continue;
        const int row = k < 0 ? 3 : s.row;
        const int top = rowY(row) + sy;
        if (k >= 0 && s.gap) {
            // A gap: hazard light below.
            canvas.fillAlpha({x0 + 4, kGroundY - 8, x1 - x0 - 8, 4}, p.danger, 90 + static_cast<int>(60 * pulse));
            continue;
        }
        const uint32_t edge = p.row[row];
        canvas.fill({x0, top + 2, x1 - x0, 9}, p.platform);
        canvas.fillAlpha({x0, top + 11, x1 - x0, 6}, p.platform, 120);
        canvas.hline(x0, x1 - 1, top, edge);
        canvas.hline(x0, x1 - 1, top + 1, Palette::mix(edge, p.platform, 120));
        canvas.fillAlpha({x0, top + 2, x1 - x0, 4}, edge, 60);
        // Support light down to the floor.
        canvas.vline(x0, top + 12, kGroundY - 1, Palette::mix(p.platform, p.ground, 128));
        if (k >= 0 && s.cue) {
            const bool pending = k == cueSlot_;
            const int a = pending ? 150 + static_cast<int>(100 * pulse) : 110;
            // Chevron on the leading edge, in the row colour.
            for (int i = 0; i < 6; ++i) canvas.blend(x0 + 2 + i, top - 7 + std::abs(i - 3) + 1, edge, a);
            for (int i = 0; i < 6; ++i) canvas.blend(x0 + 2 + i, top - 4 + std::abs(i - 3) + 1, edge, a);
            canvas.glow(x0 + 4, top - 2, pending ? 9 : 6, edge, pending ? 120 : 60);
            if (s.row2 >= 0) {
                // A twin's second colour, so the long jump reads on the course too.
                canvas.hline(x0, x1 - 1, top + 2, p.row[s.row2]);
            }
            if (s.decoy >= 0) canvas.fillAlpha({x0 + 2, top + 5, 4, 4}, p.danger, 160);
        }
    }

    // Trail and runner.
    const int ry = static_cast<int>(std::lround(runnerY(beat_))) + sy;
    if (state_ == State::Playing && fallStart_ < 0 && (nowMs / 33) % 2 == 0) {
        const uint32_t trail = landed_ ? p.row[runnerRow_] : p.runnerTrail;
        particles_.push_back({static_cast<float>(kRunnerX - 4), static_cast<float>(ry - 6 - (nowSeed_() % 6)), -60.0f - (nowSeed_() % 40), 0.0f, 0.0f, 0.35f, trail, 1});
    }
    drawParticles(canvas);
    drawRunner(canvas, kRunnerX + sx, ry, nowMs);
    drawTexts(canvas);
}

void Game::drawRunner(Canvas& canvas, int x, int y, int64_t nowMs) {
    (void)nowMs;
    const Palette& p = palette_;
    const bool airborne = !landed_ || fallStart_ >= 0;
    const float ph = runPhase_ * 6.2831853f;
    const int bob = airborne ? 0 : static_cast<int>(std::fabs(std::sin(ph)) * 2);
    const int hipY = y - 12 - bob;
    const uint32_t skin = fallStart_ >= 0 ? p.danger : p.runner;
    const uint32_t glowC = p.row[runnerRow_];
    canvas.glow(x + 2, y - 12, 16, glowC, 60);
    // Two passes: a dark outline first, then the figure, so it reads on any sky.
    for (int pass = 0; pass < 2; ++pass) {
        const uint32_t c = pass == 0 ? 0x101018 : skin;
        auto seg = [&](int x0, int y0, int x1, int y1) {
            if (pass == 0) {
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy) canvas.line(x0 + dx, y0 + dy, x1 + dx, y1 + dy, c);
            } else {
                canvas.line(x0, y0, x1, y1, c);
                canvas.line(x0 + 1, y0, x1 + 1, y1, c);
            }
        };
        // Legs
        if (airborne) {
            seg(x, hipY, x - 5, hipY + 6);
            seg(x - 5, hipY + 6, x - 1, hipY + 11);
            seg(x, hipY, x + 5, hipY + 4);
            seg(x + 5, hipY + 4, x + 9, hipY + 1);
        } else {
            const int l1 = static_cast<int>(std::sin(ph) * 6), l2 = static_cast<int>(std::sin(ph + 3.14159f) * 6);
            seg(x, hipY, x + l1, y - 5);
            seg(x + l1, y - 5, x + l1 + (l1 > 0 ? 3 : -1), y);
            seg(x, hipY, x + l2, y - 5);
            seg(x + l2, y - 5, x + l2 + (l2 > 0 ? 3 : -1), y);
        }
        // Body, leaning into the run
        seg(x, hipY, x + 3, hipY - 10);
        // Arms
        const int a1 = airborne ? -7 : static_cast<int>(std::sin(ph + 3.14159f) * 5);
        const int a2 = airborne ? -6 : static_cast<int>(std::sin(ph) * 5);
        seg(x + 2, hipY - 8, x + 2 + a1, hipY - 3 + (airborne ? -5 : 0));
        seg(x + 2, hipY - 8, x + 3 + a2 + 3, hipY - 4 + (airborne ? -5 : 0));
        // Head
        canvas.disc(x + 4, hipY - 15, pass == 0 ? 5 : 4, c);
    }
    // Visor in the lane colour
    canvas.hline(x + 2, x + 7, hipY - 15, glowC);
    canvas.hline(x + 3, x + 7, hipY - 14, Palette::mix(glowC, skin, 100));
}

void Game::drawParticles(Canvas& canvas) {
    for (const Particle& p : particles_) {
        const int a = static_cast<int>(255 * (1.0f - p.life / p.max));
        if (p.size <= 1) canvas.blend(static_cast<int>(p.x), static_cast<int>(p.y), p.color, a);
        else canvas.fillAlpha({static_cast<int>(p.x) - 1, static_cast<int>(p.y) - 1, 2, 2}, p.color, a);
    }
}

void Game::drawTexts(Canvas& canvas) {
    for (const FloatText& t : texts_) {
        const int a = static_cast<int>(255 * std::clamp(1.4f - t.life * 1.5f, 0.0f, 1.0f));
        if (t.scale > 0) {
            const int w = Canvas::scaledWidth(t.text, 1);
            canvas.textScaled(static_cast<int>(t.x) - w / 2 + 1, static_cast<int>(t.y) + 1, t.text, 0x000000, 1, a / 2);
            canvas.textScaled(static_cast<int>(t.x) - w / 2, static_cast<int>(t.y), t.text, t.color, 1, a);
        } else {
            const int w = Canvas::textWidth(t.text, Font::Small);
            canvas.fillAlpha({static_cast<int>(t.x) - w / 2 - 2, static_cast<int>(t.y) - 1, w + 4, 13}, 0x000000, a / 3);
            canvas.text(static_cast<int>(t.x) - w / 2, static_cast<int>(t.y), t.text, Palette::mix(0x000000, t.color, a), Font::Small);
        }
    }
}

// MARK: - Play

void Game::drawPlay(Canvas& canvas, int64_t nowMs) {
    drawWorld(canvas, nowMs);
    drawHud(canvas, nowMs);
}

void Game::drawHud(Canvas& canvas, int64_t nowMs) {
    const Palette& p = palette_;
    // Score, big, with a shadow.
    char buf[32];
    std::snprintf(buf, sizeof buf, "%07d", score_);
    canvas.textScaled(6, 5, buf, 0x000000, 2, 120);
    canvas.textScaled(4, 3, buf, p.white, 2);
    if (song_) {
        canvas.text(5, 37, song_->name.substr(0, 20), p.textDim, Font::Small);
        // Progress through the song, with the tempo beside it.
        const double done = track_.length() > 0 ? std::clamp(static_cast<double>(slot_) / static_cast<double>(track_.length()), 0.0, 1.0) : 0.0;
        canvas.fillAlpha({5, 52, 70, 3}, p.textDim, 90);
        canvas.fill({5, 52, static_cast<int>(70 * done), 3}, p.accent);
        std::snprintf(buf, sizeof buf, "%.0f BPM%s", engine_.tempo(), assist_ < 0.999f ? " ASSIST" : "");
        canvas.text(80, 48, buf, p.textDim, Font::Small);
    } else {
        std::snprintf(buf, sizeof buf, "LEVEL %d   %.0f BPM%s", level_, track_.tempoFor(level_) * assist_, assist_ < 0.999f ? "   ASSIST" : "");
        canvas.text(5, 37, buf, p.textDim, Font::Small);
    }

    // Lives: three lights top right.
    for (int i = 0; i < 3; ++i) {
        const int x = Canvas::width - 14 - i * 14;
        const bool on = i < lives_;
        if (on) canvas.glow(x, 11, 8, p.accent, 90);
        canvas.disc(x, 11, 4, on ? p.accentHot : Palette::mix(p.textDim, 0x000000, 150));
        if (on) canvas.disc(x - 1, 10, 1, p.white);
    }
    canvas.textRight({0, 20, Canvas::width - 6, 12}, "BEST " + std::to_string(std::max(scores_.best(), score_)), p.textDim, Font::Small);

    // Combo: centre, a ring that pulses with the beat.
    if (combo_ >= 2) {
        const float pulse = static_cast<float>(1.0 - beatPhase());
        const int r = 15 + static_cast<int>(pulse * 4);
        canvas.ring(160, 24, r, 2, Palette::mix(p.accent, p.white, static_cast<int>(pulse * 120)));
        std::snprintf(buf, sizeof buf, "%d", combo_);
        const int w = Canvas::scaledWidth(buf, 1);
        canvas.textScaled(160 - w / 2, 16, buf, p.white, 1);
        canvas.textCentered({130, 42, 60, 10}, "COMBO", p.textDim, Font::Small);
        const int mult = 1 + std::min(combo_, 70) / 10;
        if (mult > 1) {
            std::snprintf(buf, sizeof buf, "x%d", mult);
            canvas.text(182, 18, buf, p.accentHot, Font::Small);
        }
    }

    // The pads, mirrored bottom left, so the strip can be learnt on screen.
    drawPadMirror(canvas, 6, 197, 9, nowMs);
    // Judgement window hint: the beat line under the runner.
    const double phase = beatPhase();
    const int lineA = static_cast<int>(200 * (1.0 - phase));
    canvas.fillAlpha({kRunnerX - 1, kGroundY - 16, 3, 14}, p.white, lineA / 2);
}

void Game::drawPadMirror(Canvas& canvas, int x, int y, int cell, int64_t nowMs) {
    const int gap = 2;
    canvas.fillAlpha({x - 3, y - 3, 4 * (cell + gap) + 4, 4 * (cell + gap) + 4}, 0x000000, 110);
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
            const int pad = padIndex(row, col);
            const uint32_t c = padColor(pad, nowMs);
            const Rect r{x + col * (cell + gap), y + row * (cell + gap), cell, cell};
            if (c == 0) canvas.fillAlpha(r, palette_.textDim, 40);
            else {
                canvas.fill(r, c);
                canvas.glow(r.x + cell / 2, r.y + cell / 2, cell, c, 60);
            }
        }
    // The left column is "now": a thin bracket.
    canvas.vline(x - 2, y, y + 4 * (cell + gap) - gap - 1, palette_.white);
}

// MARK: - Menus

void Game::drawLogo(Canvas& canvas, int cx, int y, int scale, int64_t nowMs) {
    const Palette& p = palette_;
    const char* word = "LIGHTFOOT";
    const int w = Canvas::scaledWidth(word, scale);
    const int x = cx - w / 2;
    const int bob = static_cast<int>(std::sin(nowMs * 0.0025) * 2);
    canvas.glow(cx, y + Canvas::scaledHeight(scale) / 2 + bob, w / 2 + 10, p.accent, 70);
    // Chromatic fringe, then the word itself.
    canvas.textScaled(x - 2, y + bob, word, p.row[1], scale, 150);
    canvas.textScaled(x + 2, y + bob, word, p.row[0], scale, 150);
    canvas.textScaled(x, y + 2 + bob, word, 0x000000, scale, 120);
    canvas.textScaled(x, y + bob, word, p.white, scale);
    // Underline sweep.
    const int ux = x + static_cast<int>((nowMs / 8) % (w + 40)) - 20;
    canvas.hline(x, x + w, y + Canvas::scaledHeight(scale) + 3 + bob, p.accent);
    canvas.fillAlpha({ux, y + Canvas::scaledHeight(scale) + 2 + bob, 20, 3}, p.white, 200);
}

void Game::drawTitle(Canvas& canvas, int64_t nowMs) {
    const Palette& p = palette_;
    // The city rolls by on its own; the runner jogs on a flat course.
    beat_ = engine_.positionBeats();
    drawSky(canvas, nowMs, beat_ * kSlotW);
    const int top = rowY(3);
    canvas.fill({0, top + 2, Canvas::width, 9}, p.platform);
    canvas.hline(0, Canvas::width - 1, top, p.row[3]);
    canvas.fillAlpha({0, top + 2, Canvas::width, 4}, p.row[3], 60);
    for (int i = 0; i < 8; ++i) canvas.hline(0, Canvas::width - 1, kGroundY + i * i, p.gridLine);
    runnerRow_ = 3;
    landed_ = true;
    fallStart_ = -1;
    drawParticles(canvas);
    drawRunner(canvas, kRunnerX, top, nowMs);

    drawLogo(canvas, 160, 28, 3, nowMs);
    {
        const char* tag = "RUN THE LIGHTS  -  JUMP ON THE PADS";
        const int w = Canvas::textWidth(tag, Font::Small);
        canvas.fillAlpha({(Canvas::width - w) / 2 - 8, 82, w + 16, 14}, 0x000000, 120);
        canvas.textCentered({0, 83, Canvas::width, 12}, tag, p.text, Font::Small);
    }

    static const char* const items[5] = {"PLAY", "SONGS", "HOW TO PLAY", "HIGH SCORES", "QUIT TO STOCK"};
    const Rect panel{176, 100, 136, 88};
    canvas.fillAlpha(panel, 0x000000, 140);
    canvas.hline(panel.x, panel.right() - 1, panel.y, Palette::mix(p.accent, 0x000000, 100));
    for (int i = 0; i < 5; ++i) {
        const bool sel = i == menuCursor_;
        const int y = panel.y + 6 + i * 16;
        if (sel) {
            canvas.fillAlpha({panel.x + 4, y - 2, panel.w - 8, 15}, p.accent, 80);
            canvas.text(panel.x + 8, y, ">", p.accentHot, Font::Small);
        }
        canvas.text(panel.x + 20, y, items[i], sel ? p.white : p.textDim, Font::Small);
    }
    // Soft keys and the footer.
    canvas.text(26, 1, "PLAY", p.textDim, Font::Small);
    canvas.text(124, 1, "SONGS", p.textDim, Font::Small);
    canvas.text(236, 1, "SCORES", p.textDim, Font::Small);
    char buf[96];
    if (song_) std::snprintf(buf, sizeof buf, "SONG  %.20s     BEST %d", song_->name.c_str(), scores_.best());
    else std::snprintf(buf, sizeof buf, "ENDLESS     BEST %d     START LEVEL %d (K3)", scores_.best(), startLevel_);
    canvas.textCentered({0, 226, Canvas::width, 12}, buf, Palette::mix(p.textDim, p.white, 60), Font::Small);
    const float blink = 0.5f + 0.5f * std::sin(nowMs * 0.005f);
    canvas.textCentered({0, 210, Canvas::width, 12}, "PRESS PLAY", Palette::mix(p.textDim, p.white, static_cast<int>(blink * 255)), Font::Small);
    drawTexts(canvas);
}

void Game::drawHowTo(Canvas& canvas, int64_t nowMs) {
    const Palette& p = palette_;
    beat_ = engine_.positionBeats();
    drawSky(canvas, nowMs, beat_ * kSlotW);
    canvas.fillAlpha({0, 0, Canvas::width, Canvas::height}, 0x000000, 150);
    canvas.textScaled(10, 6, "HOW TO PLAY", p.white, 1);
    const char* lines[] = {
        "The course scrolls at the",
        "music's tempo. The 16 pads are",
        "the next four beats: a column",
        "is one beat, the left column",
        "is NOW. The lit pad in each",
        "column is the platform you",
        "must land on. When it reaches",
        "the left column, hit it ON THE",
        "BEAT. Only the lit pad works.",
        "",
        "Two lit pads: hit both for a",
        "long jump. A dim red pad is a",
        "decoy: leave it. Miss and you",
        "fall: three lives. Levels every",
        "32 beats. Fader down: slower,",
        "half points.",
    };
    int y = 28;
    for (const char* l : lines) {
        canvas.text(10, y, l, p.text, Font::Small);
        y += 11;
    }
    // A little example strip: four columns, one lit pad each.
    const int gx = 236, gy = 104, cell = 10;
    static const int demoRows[4] = {3, 1, 2, 0};
    canvas.fillAlpha({gx - 8, gy - 8, 4 * (cell + 2) + 14, 4 * (cell + 2) + 30}, 0x000000, 110);
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            const Rect r{gx + col * (cell + 2), gy + row * (cell + 2), cell, cell};
            const bool lit = demoRows[col] == row;
            if (lit) {
                canvas.fill(r, Palette::scale(p.row[row], col == 0 ? 1.0f : 0.45f));
                if (col == 0) canvas.glow(r.x + 5, r.y + 5, 9, p.row[row], 90);
            } else {
                canvas.fillAlpha(r, p.textDim, 40);
            }
        }
    canvas.vline(gx - 3, gy, gy + 4 * (cell + 2) - 3, p.white);
    canvas.text(gx - 4, gy + 52, "NOW +1 +2 +3", p.textDim, Font::Small);
    canvas.textCentered({0, 226, Canvas::width, 12}, "B1 BACK     PLAY TO START", p.textDim, Font::Small);
}

void Game::drawScores(Canvas& canvas, int64_t nowMs) {
    const Palette& p = palette_;
    beat_ = engine_.positionBeats();
    drawSky(canvas, nowMs, beat_ * kSlotW);
    canvas.fillAlpha({0, 0, Canvas::width, Canvas::height}, 0x000000, 150);
    canvas.textScaled(10, 6, "HIGH SCORES", p.white, 1);
    const auto& list = scores_.entries();
    if (list.empty()) canvas.text(10, 36, "No runs yet. Press PLAY.", p.textDim, Font::Small);
    else canvas.text(10, 30, "#    SCORE   LV  COMBO  SONG          DATE", p.textDim, Font::Small);
    for (size_t i = 0; i < list.size() && i < 8; ++i) {
        char line[96];
        std::snprintf(line, sizeof line, "%zu  %7d   %2d   %3d   %-12.12s  %s", i + 1, list[i].score, list[i].level, list[i].combo,
                      list[i].song.empty() ? "endless" : list[i].song.c_str(), list[i].date.c_str());
        const bool fresh = static_cast<int>(i) == finalRank_ && state_ == State::Scores;
        canvas.text(10, 46 + static_cast<int>(i) * 14, line, fresh ? p.accentHot : i == 0 ? p.white : p.text, Font::Small);
    }
    canvas.textCentered({0, 226, Canvas::width, 12}, "B1 BACK     PLAY TO START", p.textDim, Font::Small);
}

void Game::drawSongs(Canvas& canvas, int64_t nowMs) {
    const Palette& p = palette_;
    beat_ = engine_.positionBeats();
    drawSky(canvas, nowMs, beat_ * kSlotW);
    canvas.fillAlpha({0, 0, Canvas::width, Canvas::height}, 0x000000, 150);
    canvas.textScaled(10, 6, "SONGS", p.white, 1);
    canvas.textRight({0, 8, Canvas::width - 10, 12}, "Lightfoot/Songs on the card", p.textDim, Font::Small);
    const int rows = static_cast<int>(songFiles_.size()) + 2;
    const int visible = 12;
    const int first = std::max(0, songCursor_ - visible + 2);
    for (int i = first; i < rows && i - first < visible; ++i) {
        const int y = 30 + (i - first) * 14;
        const bool sel = i == songCursor_;
        std::string label;
        uint32_t color = p.text;
        if (i == 0) {
            label = "ENDLESS COURSE";
            color = p.row[3];
        } else if (i == rows - 1) {
            label = "BROWSE THE CARD...";
            color = p.accentHot;
        } else {
            label = songFiles_[static_cast<size_t>(i - 1)];
            const std::string::size_type dot = label.rfind('.');
            if (dot != std::string::npos) label = label.substr(0, dot);
            if (song_ && label == song_->name) color = p.accent;
        }
        if (sel) {
            canvas.fillAlpha({6, y - 2, Canvas::width - 12, 14}, p.accent, 70);
            canvas.text(10, y, ">", p.accentHot, Font::Small);
        }
        canvas.textClipped({0, y, Canvas::width - 10, 12}, 22, y, label, sel ? p.white : color, Font::Small);
    }
    const char* hint = songCursor_ == 0 ? "The generated course, levels and all." : songCursor_ == rows - 1 ? "Any .mid on the SD card or internal storage." : "Press to play. The tune picks the platforms.";
    canvas.textCentered({0, 208, Canvas::width, 12}, hint, p.textDim, Font::Small);
    canvas.textCentered({0, 226, Canvas::width, 12}, "B1 BACK     PRESS OR PLAY TO CHOOSE", p.textDim, Font::Small);
}

void Game::drawPaused(Canvas& canvas, int64_t nowMs) {
    (void)nowMs;
    const Palette& p = palette_;
    canvas.fillAlpha(kScreen, 0x000000, 150);
    const char* word = "PAUSED";
    canvas.textScaled((Canvas::width - Canvas::scaledWidth(word, 2)) / 2, 86, word, p.white, 2);
    canvas.textCentered({0, 130, Canvas::width, 12}, "PLAY  RESUME      STOP  QUIT RUN", p.textDim, Font::Small);
}

void Game::drawGameOver(Canvas& canvas, int64_t nowMs) {
    const Palette& p = palette_;
    const int64_t age = nowMs - stateSince_;
    canvas.fillAlpha(kScreen, 0x000000, static_cast<int>(std::min<int64_t>(170, age / 3)));
    const bool best = finalRank_ == 0;
    const char* word = songComplete_ ? "SONG CLEAR" : best ? "NEW BEST" : "GAME OVER";
    const int w = Canvas::scaledWidth(word, 2);
    canvas.glow(160, 70, 70, best || songComplete_ ? p.accent : p.danger, 60);
    canvas.textScaled((Canvas::width - w) / 2 + 2, 56, word, 0x000000, 2, 150);
    canvas.textScaled((Canvas::width - w) / 2, 54, word, best ? p.accentHot : p.white, 2);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d", score_);
    const int sw = Canvas::scaledWidth(buf, 2);
    canvas.textScaled((Canvas::width - sw) / 2, 92, buf, p.white, 2);
    std::snprintf(buf, sizeof buf, "LEVEL %d    BEST COMBO %d", level_, maxCombo_);
    canvas.textCentered({0, 128, Canvas::width, 12}, buf, p.text, Font::Small);
    std::snprintf(buf, sizeof buf, "PERFECT %d   GOOD %d   MISS %d   SLIP %d", perfects_, goods_, misses_, slips_);
    canvas.textCentered({0, 142, Canvas::width, 12}, buf, p.textDim, Font::Small);
    if (finalRank_ >= 0) {
        std::snprintf(buf, sizeof buf, "#%d ON THE CARD", finalRank_ + 1);
        canvas.textCentered({0, 158, Canvas::width, 12}, buf, p.accentHot, Font::Small);
    }
    canvas.textCentered({0, 210, Canvas::width, 12}, "PLAY  AGAIN      STOP  TITLE", p.textDim, Font::Small);
}

// MARK: - Panel

uint32_t Game::padColor(int pad, int64_t nowMs) const {
    const Palette& p = palette_;
    const size_t idx = static_cast<size_t>(pad);
    const int64_t flashAge = nowMs - padFlashMs_[idx];
    if (padFlashMs_[idx] > 0 && flashAge < 160) {
        return Palette::mix(padFlashColor_[idx], 0x000000, static_cast<int>(flashAge * 255 / 160));
    }
    const int row = padRow(pad), col = padCol(pad);
    switch (state_) {
    case State::Playing:
    case State::Paused: {
        const int64_t k = columnBase() + col;
        if (k < 0) return 0;
        const Slot& s = track_.at(k);
        if (!s.cue) return 0;
        const bool lit = row == s.row || row == s.row2;
        if (lit) {
            float level;
            if (col == 0) level = 0.55f + 0.45f * static_cast<float>(1.0 - beatPhase());
            else level = 0.42f - col * 0.1f;
            if (state_ == State::Paused) level *= 0.3f;
            if (twinHalf_ && col == 0 && row == twinHalfRow_) level = 1.0f;
            return Palette::scale(p.row[row], level);
        }
        if (row == s.decoy) return col == 0 ? 0x5a0000 : 0x2a0000;
        return 0;
    }
    case State::GameOver: {
        const float t = 0.5f + 0.5f * std::sin(nowMs * 0.004f + row * 0.6f);
        return Palette::scale(p.danger, 0.08f + 0.25f * t);
    }
    default: {
        // The title's slow wave across the rows, in the row colours.
        const float t = 0.5f + 0.5f * std::sin(titlePhase_ * 2.4f - col * 0.9f + row * 0.5f);
        return Palette::scale(p.row[row], 0.05f + 0.45f * t * t);
    }
    }
}

void Game::drawPanelLights(Canvas& canvas, int64_t nowMs) {
    for (int pad = 0; pad < 16; ++pad) canvas.padLed(pad, padColor(pad, nowMs));
    const bool playing = state_ == State::Playing;
    canvas.buttonLed(IN_PLAY, playing ? 2 : 1);
    canvas.buttonLed(IN_STOP, state_ == State::Title ? 0 : 1);
    canvas.buttonLed(IN_SHIFT, 1);
    // TAP TEMPO is the metronome: bright on the beat.
    const double phase = state_ == State::Playing ? beatPhase() : std::fmod(engine_.positionBeats(), 1.0);
    canvas.buttonLed(IN_TAP_TEMPO, phase < 0.12 ? 2 : 0);
    // On a level-up the right-hand column chases upwards.
    const bool chase = !banner_.empty() && nowMs < bannerUntil_ && banner_.rfind("LEVEL", 0) == 0;
    static const int column[] = {IN_MUTE, IN_ERASE, IN_CHOP, IN_LOOP, IN_SAMPLE_REC, IN_SEQ_REC};
    const int lit = chase ? static_cast<int>((nowMs / 70) % 6) : -1;
    for (int i = 0; i < 6; ++i) canvas.buttonLed(column[i], i == lit ? 2 : 0);
    for (int b : {IN_SEQ, IN_SAMPLE, IN_KNOB_FX, IN_PAD_FX, IN_PAD_BANK, IN_NOTE_REPEAT, IN_LEVELS, IN_SAMPLE_SELECT, IN_MINUS, IN_PLUS})
        canvas.buttonLed(b, 0);
    // Meters: combo heat on the left, the kick on the right.
    const int heat = playing ? std::min(5, combo_ / 6) : static_cast<int>(engine_.peakLeft() * 6);
    canvas.meters(heat, static_cast<int>(engine_.kickEnvelope() * 5.99f));
    canvas.trackLed(playing || state_ == State::Paused ? (level_ - 1) % 8 : static_cast<int>((nowMs / 350) % 8));
}

} // namespace lightfoot
