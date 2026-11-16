// Everything Lightfoot draws: the night behind the highway, the highway and
// its gems, the frets, the HUD, the menus, and the lights on the panel.
// Game.cpp holds the rules.
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

constexpr int kHorizon = 150;        // where the city meets the ground, behind the highway
constexpr int kVanishX = 160;        // the highway's vanishing point
constexpr int kVanishY = 58;
constexpr int kStrikeY = 196;        // the strike line
constexpr int kHalfWidthAtStrike = 118;
constexpr double kDepthScale = 0.42; // how quickly steps recede
constexpr double kStepsShown = 7.5;  // steps of highway drawn ahead of the strike line
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

// MARK: - Highway geometry

double Game::yForDepth(double z) const {
    // z is steps ahead of the strike line; 0 is the line, negative has passed it.
    const double scale = 1.0 / (1.0 + std::max(-0.9, z) * kDepthScale);
    return kVanishY + (kStrikeY - kVanishY) * scale;
}

double Game::halfWidthAtY(double y) const {
    return kHalfWidthAtStrike * (y - kVanishY) / (kStrikeY - kVanishY);
}

double Game::laneX(int lane, double y) const {
    return kVanishX + (lane - 1.5) * halfWidthAtY(y) / 2.0;
}

int Game::strikeX(int lane) const {
    return static_cast<int>(std::lround(laneX(lane, kStrikeY)));
}

int Game::strikeY() const {
    return kStrikeY;
}

// MARK: - Frame

void Game::draw(Canvas& canvas, const Frame& in, int64_t nowMs) {
    switch (state_) {
    case State::Title: drawTitle(canvas, nowMs); break;
    case State::HowTo: drawHowTo(canvas, nowMs); break;
    case State::Scores: drawScores(canvas, nowMs); break;
    case State::Songs: drawSongs(canvas, nowMs); break;
    case State::SongInfo: drawSongInfo(canvas, nowMs); break;
    case State::Browse: break; // the toolkit browser drew itself during update
    case State::Playing: drawPlay(canvas, in, nowMs); break;
    case State::Paused:
        drawPlay(canvas, in, nowMs);
        drawPaused(canvas, nowMs);
        break;
    case State::GameOver:
        drawPlay(canvas, in, nowMs);
        drawGameOver(canvas, nowMs);
        break;
    }
    if (flash_ > 0) canvas.fillAlpha(kScreen, flashColor_, static_cast<int>(flash_ * 160));
    if (!banner_.empty() && nowMs < bannerUntil_) {
        const int64_t left = bannerUntil_ - nowMs;
        const int alpha = static_cast<int>(std::min<int64_t>(255, left * 255 / 400));
        const int scale = banner_.size() <= 2 ? 4 : 2;
        const int w = Canvas::scaledWidth(banner_, scale);
        const int x = (Canvas::width - w) / 2, y = 100 - Canvas::scaledHeight(scale) / 2;
        canvas.fillAlpha({x - 10, y - 6, w + 20, Canvas::scaledHeight(scale) + 12}, 0x000000, alpha / 2);
        canvas.textScaled(x + 2, y + 2, banner_, palette_.accent, scale, alpha / 2);
        canvas.textScaled(x, y, banner_, palette_.white, scale, alpha);
    }
}

// MARK: - The night

void Game::drawSky(Canvas& canvas, int64_t nowMs, double scroll) {
    const Palette& p = palette_;
    for (int y = 0; y < Canvas::height; ++y) {
        const int t = std::min(255, y * 255 / kStrikeY);
        const uint32_t c = t < 150 ? Palette::mix(p.skyTop, p.skyMid, t * 255 / 150) : Palette::mix(p.skyMid, p.skyHorizon, (t - 150) * 255 / 105);
        canvas.hline(0, Canvas::width - 1, y, c);
    }
    for (uint32_t i = 0; i < 70; ++i) {
        const int sx = static_cast<int>(std::fmod(hashUnit(i * 7 + 1) * 400 - scroll * 0.6 + 4000, 400.0)) - 40;
        const int sy = static_cast<int>(hashUnit(i * 7 + 2) * 110);
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
    const int sunX = 160, sunY = 92, sunR = 44;
    canvas.glow(sunX, sunY, sunR + 30, p.sun, 110);
    for (int dy = -sunR; dy <= sunR; ++dy) {
        const int y = sunY + dy;
        if (y >= kHorizon) break;
        const int half = static_cast<int>(std::sqrt(static_cast<double>(sunR * sunR - dy * dy)));
        if (dy > 0 && ((dy + static_cast<int>(nowMs / 90)) % std::max(3, 10 - dy / 5)) < std::min(3, 1 + dy / 14)) continue;
        const uint32_t c = Palette::mix(p.sunCore, p.sun, std::clamp((dy + sunR) * 255 / (2 * sunR), 0, 255));
        canvas.hline(sunX - half, sunX + half, y, c);
    }
    for (int x = 0; x < Canvas::width; ++x) {
        const double wx = x + scroll * 0.05;
        const double h = 22 * std::sin(wx * 0.021) + 12 * std::sin(wx * 0.053 + 1.3) + 6 * std::sin(wx * 0.11 + 0.4);
        canvas.vline(x, kHorizon - 22 - static_cast<int>(h), kHorizon, p.mountain);
    }
    for (int layer = 0; layer < 2; ++layer) {
        const double par = layer == 0 ? 0.12 : 0.22;
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
            canvas.fill({x0, top, w, kStrikeY - top}, body);
            if (hashUnit(hb + 3) > 0.5f) canvas.fill({x0 + w / 2 - 1, top - 6, 2, 6}, body);
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
    canvas.hline(0, Canvas::width - 1, kHorizon, Palette::mix(p.gridLineBright, p.skyHorizon, 120));
    canvas.fill({0, kHorizon + 1, Canvas::width, Canvas::height - kHorizon - 1}, p.ground);
    const double phase = stepPhase();
    for (int i = 0; i < 8; ++i) {
        const double t = std::fmod(i + phase, 8.0) / 8.0;
        const int y = kHorizon + 1 + static_cast<int>(t * t * (Canvas::height - kHorizon));
        canvas.hline(0, Canvas::width - 1, y, p.gridLine);
    }
}

// MARK: - The highway

void Game::drawHighway(Canvas& canvas, const Frame& in, int64_t nowMs) {
    const Palette& p = palette_;
    const int sx = shake_ > 0 ? static_cast<int>(std::sin(nowMs * 0.09) * shake_ * 3) : 0;
    const float pulse = 0.5f + 0.5f * static_cast<float>(1.0 - beatPhase());

    // The road: a dark trapezoid from the vanishing point to the strike line and beyond.
    for (int y = kVanishY; y < Canvas::height; ++y) {
        const double hw = halfWidthAtY(std::min<double>(y, kStrikeY + 30));
        const int x0 = static_cast<int>(kVanishX - hw) + sx, x1 = static_cast<int>(kVanishX + hw) + sx;
        const int depthShade = std::clamp((y - kVanishY) * 255 / (kStrikeY - kVanishY), 0, 255);
        canvas.hline(x0, x1, y, Palette::mix(p.skyMid, 0x0a0912, 140 + depthShade / 3));
    }
    // Beat lines sliding down, bar lines brighter.
    const int64_t firstStep = static_cast<int64_t>(std::floor(pos_)) - 1;
    const double stepsPerBar = track_.beatsPerBar() / stepBeats();
    for (int64_t k = firstStep; k < firstStep + static_cast<int64_t>(kStepsShown) + 2; ++k) {
        const double z = static_cast<double>(k) - pos_;
        if (z < -0.6 || z > kStepsShown) continue;
        const int y = static_cast<int>(std::lround(yForDepth(z)));
        const double hw = halfWidthAtY(y);
        const bool bar = std::fmod(static_cast<double>(k), stepsPerBar) == 0;
        const bool beat = std::fmod(static_cast<double>(k) * stepBeats(), 1.0) == 0;
        if (!bar && !beat) continue;
        const int a = static_cast<int>(60 + 120 * (y - kVanishY) / static_cast<double>(kStrikeY - kVanishY));
        canvas.fillAlpha({static_cast<int>(kVanishX - hw) + sx, y, static_cast<int>(2 * hw), 1}, bar ? p.gridLineBright : p.gridLine, bar ? a : a / 2);
    }
    // Lane dividers and the glowing edges.
    for (int l = 0; l <= 4; ++l) {
        const double xTop = kVanishX + (l - 2) * halfWidthAtY(kVanishY + 6) / 2.0;
        const double xBottom = kVanishX + (l - 2) * kHalfWidthAtStrike / 2.0;
        const bool edge = l == 0 || l == 4;
        canvas.line(static_cast<int>(xTop) + sx, kVanishY + 6, static_cast<int>(xBottom) + sx, Canvas::height - 1, edge ? p.gridLineBright : Palette::mix(p.gridLine, 0x000000, 60));
        if (edge) canvas.line(static_cast<int>(xTop) + sx + (l ? -1 : 1), kVanishY + 6, static_cast<int>(xBottom) + sx + (l ? -1 : 1), Canvas::height - 1, Palette::mix(p.gridLineBright, 0x000000, 150));
    }

    // Gems, far to near so the close ones draw on top. Sustains draw as tails.
    const int64_t lastStep = firstStep + static_cast<int64_t>(kStepsShown) + 2;
    for (int64_t k = lastStep; k >= firstStep; --k) {
        if (k < 0) continue;
        const Slot s = track_.at(k);
        if (!s.cue) continue;
        const double z = static_cast<double>(k) - pos_;
        if (z > kStepsShown || z < -1.0) continue;
        const bool pending = k == cueSlot_;
        const bool hitAlready = k == lastHitSlot_ && lastHit_ != Hit::Miss && k <= resolvedUpTo_;
        if (hitAlready) continue; // it burst into flames when it was hit
        const bool passed = k <= resolvedUpTo_ && !pending; // missed: it slides on, grey
        const double y = yForDepth(z);
        const double depth = (y - kVanishY) / static_cast<double>(kStrikeY - kVanishY);
        const int r = 3 + static_cast<int>(std::lround(7 * depth));
        auto gem = [&](int lane, uint32_t color, bool ghost) {
            const int x = static_cast<int>(std::lround(laneX(lane, y))) + sx;
            const int yy = static_cast<int>(std::lround(y));
            if (s.noteBeats >= 1.0 && !ghost) {
                const double zEnd = z + s.noteBeats / stepBeats();
                const double yEnd = yForDepth(std::min(zEnd, kStepsShown));
                const int xEnd = static_cast<int>(std::lround(laneX(lane, yEnd))) + sx;
                for (int i = -1; i <= 1; ++i) canvas.line(x + i, yy, xEnd + i / 2, static_cast<int>(yEnd), Palette::mix(color, 0x000000, 90));
            }
            if (ghost) {
                canvas.discAlpha(x, yy, r, p.textDim, 110);
                return;
            }
            canvas.glow(x, yy, r + 5, color, pending ? 90 + static_cast<int>(90 * pulse) : 60);
            canvas.disc(x, yy, r, Palette::mix(color, 0x000000, 60));
            canvas.disc(x, yy, std::max(1, r - 2), color);
            canvas.disc(x - r / 3, yy - r / 3, std::max(1, r / 3), Palette::mix(color, p.white, 170));
            if (pending && depth > 0.85) canvas.ring(x, yy, r + 3, 1, Palette::mix(color, p.white, static_cast<int>(200 * pulse)));
        };
        const int lane = laneOf(s.row);
        if (s.row2 >= 0) {
            const int lane2 = laneOf(s.row2);
            const int xa = static_cast<int>(std::lround(laneX(std::min(lane, lane2), y))) + sx;
            const int xb = static_cast<int>(std::lround(laneX(std::max(lane, lane2), y))) + sx;
            canvas.fillAlpha({xa, static_cast<int>(y) - 1, xb - xa, 3}, p.white, passed ? 60 : 160);
            gem(lane2, p.row[lane2], passed);
        }
        gem(lane, p.row[lane], passed);
        if (s.decoy >= 0) {
            const int dl = laneOf(s.decoy);
            const int x = static_cast<int>(std::lround(laneX(dl, y))) + sx;
            const int yy = static_cast<int>(std::lround(y));
            canvas.disc(x, yy, r, 0x3a0a10);
            canvas.ring(x, yy, r, 1, p.danger);
            canvas.line(x - r / 2, yy - r / 2, x + r / 2, yy + r / 2, p.danger);
            canvas.line(x - r / 2, yy + r / 2, x + r / 2, yy - r / 2, p.danger);
        }
    }

    // The frets at the strike line: rings in the lane colours, lit while held, flashing on a hit.
    for (int lane = 0; lane < 4; ++lane) {
        const int x = strikeX(lane) + sx;
        bool held = false;
        for (int row = 0; row < 4; ++row) held |= in.padDown(padIndex(row, lane));
        const int64_t flashAge = nowMs - fretFlashMs_[static_cast<size_t>(lane)];
        const bool flash = fretFlashMs_[static_cast<size_t>(lane)] > 0 && flashAge < 140;
        const uint32_t c = p.row[lane];
        canvas.fillAlpha({x - 13, kStrikeY - 4, 26, 8}, c, held || flash ? 110 : 50);
        canvas.ring(x, kStrikeY, 10, 2, held || flash ? p.white : c);
        canvas.disc(x, kStrikeY, 7, held || flash ? Palette::mix(c, p.white, flash ? 200 : 90) : Palette::mix(c, 0x000000, 150));
        if (flash) canvas.ring(x, kStrikeY, 10 + static_cast<int>(flashAge / 10), 1, Palette::mix(c, 0x000000, static_cast<int>(flashAge * 255 / 140)));
    }
    canvas.hline(static_cast<int>(kVanishX - kHalfWidthAtStrike) + sx, static_cast<int>(kVanishX + kHalfWidthAtStrike) + sx, kStrikeY, Palette::mix(p.white, p.gridLineBright, 120));
    drawParticles(canvas);
    drawTexts(canvas);
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

void Game::drawPlay(Canvas& canvas, const Frame& in, int64_t nowMs) {
    drawSky(canvas, nowMs, pos_ * 40);
    drawHighway(canvas, in, nowMs);
    drawHud(canvas, nowMs);
}

void Game::drawHud(Canvas& canvas, int64_t nowMs) {
    const Palette& p = palette_;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%d", score_);
    const int sw = Canvas::scaledWidth(buf, 2);
    canvas.textScaled(Canvas::width - 6 - sw + 2, 5, buf, 0x000000, 2, 120);
    canvas.textScaled(Canvas::width - 6 - sw, 3, buf, p.white, 2);
    const int mult = 1 + std::min(combo_, 70) / 10;
    std::snprintf(buf, sizeof buf, "x%d", mult);
    canvas.textScaled(Canvas::width - 6 - Canvas::scaledWidth(buf, 1), 36, buf, mult > 1 ? p.accentHot : p.textDim, 1);

    if (song_) {
        canvas.text(5, 4, song_->name.substr(0, 22), p.text, Font::Small);
        std::snprintf(buf, sizeof buf, "%s  LEVEL %d  %.0f BPM%s", difficultyName(difficulty_), level_, engine_.tempo(), assist_ < 0.999f ? "  ASSIST" : "");
        canvas.text(5, 16, buf, p.textDim, Font::Small);
        const double done = track_.length() > 0 ? std::clamp(static_cast<double>(slot_) / static_cast<double>(track_.length()), 0.0, 1.0) : 0.0;
        canvas.fillAlpha({5, 30, 110, 3}, p.textDim, 90);
        canvas.fill({5, 30, static_cast<int>(110 * done), 3}, p.accent);
    } else {
        canvas.text(5, 4, "ENDLESS", p.text, Font::Small);
        std::snprintf(buf, sizeof buf, "%s  LEVEL %d  %.0f BPM%s", difficultyName(difficulty_), level_, track_.tempoFor(level_) * assist_, assist_ < 0.999f ? "  ASSIST" : "");
        canvas.text(5, 16, buf, p.textDim, Font::Small);
    }

    // Rock meter: left edge, red to green, with a needle.
    const Rect meter{8, 70, 10, 110};
    canvas.fillAlpha({meter.x - 3, meter.y - 3, meter.w + 6, meter.h + 6}, 0x000000, 120);
    for (int y = 0; y < meter.h; ++y) {
        const float t = 1.0f - static_cast<float>(y) / meter.h;
        const uint32_t c = t < 0.33f ? p.danger : t < 0.66f ? p.row[2] : p.row[0];
        canvas.fillAlpha({meter.x, meter.y + y, meter.w, 1}, c, t <= rock_ ? 230 : 50);
    }
    const int needleY = meter.y + meter.h - static_cast<int>(rock_ * meter.h);
    canvas.fill({meter.x - 2, needleY - 1, meter.w + 4, 2}, p.white);
    if (rock_ < 0.25f && (nowMs / 250) % 2 == 0) canvas.text(meter.x - 2, meter.bottom() + 4, "!!", p.danger, Font::Small);

    if (combo_ >= 2) {
        std::snprintf(buf, sizeof buf, "%d", combo_);
        canvas.fillAlpha({26, 206, 64, 28}, 0x000000, 110);
        canvas.textScaled(30, 208, buf, p.white, 1);
        canvas.text(30 + Canvas::scaledWidth(buf, 1) + 4, 213, "STREAK", p.textDim, Font::Small);
    }
    const int judged = perfects_ + goods_ + misses_;
    if (judged > 0) {
        std::snprintf(buf, sizeof buf, "%d%%", (perfects_ + goods_) * 100 / judged);
        canvas.textRight({0, 212, Canvas::width - 8, 12}, buf, p.textDim, Font::Small);
    }
}

// MARK: - Menus

void Game::drawLogo(Canvas& canvas, int cx, int y, int scale, int64_t nowMs) {
    const Palette& p = palette_;
    const char* word = "LIGHTFOOT";
    const int w = Canvas::scaledWidth(word, scale);
    const int x = cx - w / 2;
    const int bob = static_cast<int>(std::sin(nowMs * 0.0025) * 2);
    canvas.glow(cx, y + Canvas::scaledHeight(scale) / 2 + bob, w / 2 + 10, p.accent, 70);
    canvas.textScaled(x - 2, y + bob, word, p.row[1], scale, 150);
    canvas.textScaled(x + 2, y + bob, word, p.row[3], scale, 150);
    canvas.textScaled(x, y + 2 + bob, word, 0x000000, scale, 120);
    canvas.textScaled(x, y + bob, word, p.white, scale);
    const int ux = x + static_cast<int>((nowMs / 8) % (w + 40)) - 20;
    canvas.hline(x, x + w, y + Canvas::scaledHeight(scale) + 3 + bob, p.accent);
    canvas.fillAlpha({ux, y + Canvas::scaledHeight(scale) + 2 + bob, 20, 3}, p.white, 200);
}

void Game::drawTitle(Canvas& canvas, int64_t nowMs) {
    const Palette& p = palette_;
    beat_ = engine_.positionBeats();
    pos_ = beat_ / stepBeats();
    drawSky(canvas, nowMs, pos_ * 40);
    {
        input blank{};
        for (int i = 0; i < 16; ++i) blank.pad_hit[i] = blank.pad_pressure[i] = -1;
        Frame none(blank);
        drawHighway(canvas, none, nowMs);
    }
    drawLogo(canvas, 160, 28, 3, nowMs);
    {
        const char* tag = "HIT THE PADS AS THE GEMS LAND";
        const int w = Canvas::textWidth(tag, Font::Small);
        canvas.fillAlpha({(Canvas::width - w) / 2 - 8, 82, w + 16, 14}, 0x000000, 120);
        canvas.textCentered({0, 83, Canvas::width, 12}, tag, p.text, Font::Small);
    }
    static const char* const items[5] = {"PLAY", "SONGS", "HOW TO PLAY", "HIGH SCORES", "QUIT TO STOCK"};
    const Rect panel{176, 100, 136, 88};
    canvas.fillAlpha(panel, 0x000000, 150);
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
    canvas.text(26, 1, "PLAY", p.textDim, Font::Small);
    canvas.text(124, 1, "SONGS", p.textDim, Font::Small);
    canvas.text(236, 1, "SCORES", p.textDim, Font::Small);
    char buf[96];
    if (song_) std::snprintf(buf, sizeof buf, "%s (K3)     SONG  %.18s     BEST %d", difficultyName(difficulty_), song_->name.c_str(), scores_.best());
    else std::snprintf(buf, sizeof buf, "%s (K3)     ENDLESS     BEST %d", difficultyName(difficulty_), scores_.best());
    canvas.fillAlpha({0, 222, Canvas::width, 18}, 0x000000, 140);
    canvas.textCentered({0, 226, Canvas::width, 12}, buf, Palette::mix(p.textDim, p.white, 60), Font::Small);
    const float blink = 0.5f + 0.5f * std::sin(nowMs * 0.005f);
    canvas.textCentered({0, 208, Canvas::width, 12}, "PRESS PLAY", Palette::mix(p.textDim, p.white, static_cast<int>(blink * 255)), Font::Small);
}

void Game::drawHowTo(Canvas& canvas, int64_t nowMs) {
    const Palette& p = palette_;
    beat_ = engine_.positionBeats();
    drawSky(canvas, nowMs, beat_ * 40);
    canvas.fillAlpha({0, 0, Canvas::width, Canvas::height}, 0x000000, 150);
    canvas.textScaled(10, 6, "HOW TO PLAY", p.white, 1);
    const char* lines[] = {
        "Gems come down the highway in",
        "four lanes, at the music's tempo.",
        "The pads are the same highway:",
        "each row is a step ahead, the",
        "bottom row is the strike line,",
        "and the lit pad shows the lane.",
        "",
        "When a gem reaches the line, hit",
        "a pad in its lane, on the beat.",
        "Any pad in the column counts.",
        "Two gems joined: hit both lanes.",
        "A crossed dark gem: leave it.",
        "",
        "Misses drain the rock meter;",
        "hits fill it. Empty and you fail.",
        "K3 picks Easy to Expert. Fader",
        "down: slower, half points.",
    };
    int y = 26;
    for (const char* l : lines) {
        canvas.text(10, y, l, p.text, Font::Small);
        y += 10;
    }
    const int gx = 236, gy = 104, cell = 10;
    static const int demoLanes[4] = {2, 0, 3, 1};
    canvas.fillAlpha({gx - 8, gy - 8, 4 * (cell + 2) + 14, 4 * (cell + 2) + 30}, 0x000000, 110);
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
            const Rect r{gx + col * (cell + 2), gy + row * (cell + 2), cell, cell};
            const bool lit = demoLanes[row] == col;
            if (lit) {
                canvas.fill(r, Palette::scale(p.row[col], row == 3 ? 1.0f : 0.35f + 0.15f * row));
                if (row == 3) canvas.glow(r.x + 5, r.y + 5, 9, p.row[col], 90);
            } else {
                canvas.fillAlpha(r, p.textDim, 40);
            }
        }
    canvas.hline(gx - 3, gx + 4 * (cell + 2) - 2, gy + 3 * (cell + 2) - 2, p.white);
    canvas.text(gx - 4, gy + 52, "NOW: bottom", p.textDim, Font::Small);
    canvas.textCentered({0, 226, Canvas::width, 12}, "B1 BACK     PLAY TO START", p.textDim, Font::Small);
}

void Game::drawScores(Canvas& canvas, int64_t nowMs) {
    const Palette& p = palette_;
    beat_ = engine_.positionBeats();
    drawSky(canvas, nowMs, beat_ * 40);
    canvas.fillAlpha({0, 0, Canvas::width, Canvas::height}, 0x000000, 150);
    canvas.textScaled(10, 6, "HIGH SCORES", p.white, 1);
    const auto& list = scores_.entries();
    if (list.empty()) canvas.text(10, 36, "No runs yet. Press PLAY.", p.textDim, Font::Small);
    else canvas.text(10, 30, "#    SCORE  DIFF  LV STREAK SONG        DATE", p.textDim, Font::Small);
    for (size_t i = 0; i < list.size() && i < 8; ++i) {
        char line[96];
        std::snprintf(line, sizeof line, "%zu  %7d   %c   %2d   %3d  %-11.11s %s", i + 1, list[i].score, difficultyName(list[i].difficulty)[0], list[i].level, list[i].combo,
                      list[i].song.empty() ? "endless" : list[i].song.c_str(), list[i].date.c_str());
        const bool fresh = static_cast<int>(i) == finalRank_ && state_ == State::Scores;
        canvas.text(10, 46 + static_cast<int>(i) * 14, line, fresh ? p.accentHot : i == 0 ? p.white : p.text, Font::Small);
    }
    canvas.textCentered({0, 226, Canvas::width, 12}, "B1 BACK     PLAY TO START", p.textDim, Font::Small);
}

void Game::drawSongs(Canvas& canvas, int64_t nowMs) {
    const Palette& p = palette_;
    beat_ = engine_.positionBeats();
    drawSky(canvas, nowMs, beat_ * 40);
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
            label = "ENDLESS";
            color = p.row[0];
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
    const char* hint = songCursor_ == 0 ? "The generated chart, levels and all." : songCursor_ == rows - 1 ? "Any .mid on the SD card or internal storage." : "Press to see the song and pick the lead.";
    canvas.textCentered({0, 208, Canvas::width, 12}, hint, p.textDim, Font::Small);
    canvas.textCentered({0, 226, Canvas::width, 12}, "B1 BACK     PRESS OR PLAY TO CHOOSE", p.textDim, Font::Small);
}

void Game::drawSongInfo(Canvas& canvas, int64_t nowMs) {
    const Palette& p = palette_;
    beat_ = engine_.positionBeats();
    drawSky(canvas, nowMs, beat_ * 40);
    canvas.fillAlpha({0, 0, Canvas::width, Canvas::height}, 0x000000, 160);
    if (!song_) return;
    canvas.textScaled(10, 6, song_->name.substr(0, 19), p.white, 1);
    char buf[96];
    const int bars = static_cast<int>(song_->lengthBeats / std::max(1, song_->beatsPerBar));
    std::snprintf(buf, sizeof buf, "%.0f BPM   %d/4   %d bars   %zu notes%s", song_->tempoAt(0), song_->beatsPerBar, bars, song_->notes.size(), song_->hasDrums ? "" : "   no drums");
    canvas.text(10, 26, buf, p.textDim, Font::Small);
    canvas.text(10, 46, "LEAD INSTRUMENT  (best guess first)", p.accent, Font::Small);
    // Seven rows; the window follows the cursor so every channel can be reached.
    constexpr int kRows = 7;
    const int count = static_cast<int>(song_->candidates.size());
    const int shown = std::min(kRows, count);
    const int top = std::clamp(leadCursor_ - kRows / 2, 0, std::max(0, count - kRows));
    for (int i = top; i < top + shown; ++i) {
        const int c = song_->candidates[static_cast<size_t>(i)];
        const MidiSong::ChannelInfo& ch = song_->channels[static_cast<size_t>(c)];
        const int y = 62 + (i - top) * 14;
        const bool sel = i == leadCursor_;
        if (sel) {
            canvas.fillAlpha({6, y - 2, Canvas::width - 12, 14}, p.accent, 70);
            canvas.text(10, y, ">", p.accentHot, Font::Small);
        }
        std::snprintf(buf, sizeof buf, "ch %2d  %-15.15s %5d notes", c + 1, MidiSong::programName(ch.program), ch.notes);
        canvas.text(22, y, buf, sel ? p.white : p.text, Font::Small);
        const int w = static_cast<int>(std::clamp(ch.score / 8.0f, 0.0f, 1.0f) * 40);
        canvas.fillAlpha({270, y + 3, 40, 5}, p.textDim, 60);
        canvas.fill({270, y + 3, w, 5}, sel ? p.accentHot : p.textDim);
    }
    if (top > 0) canvas.text(Canvas::width - 14, 46, "^", p.textDim, Font::Small);
    if (top + shown < count) canvas.text(Canvas::width - 14, 62 + (kRows - 1) * 14, "v", p.textDim, Font::Small);
    std::snprintf(buf, sizeof buf, "DIFFICULTY  %s  (K3)", difficultyName(difficulty_));
    canvas.text(10, 164, buf, p.accent, Font::Small);
    static const char* const hints[4] = {"the strong beats of the tune, no chords", "every beat of the tune, chords from level 3", "every note, decoys from level 5", "every note, chords and decoys, 10% faster"};
    canvas.text(10, 176, hints[std::clamp(difficulty_, 0, 3)], p.textDim, Font::Small);
    // The audition: where the solo line is in the song, in the sixteenths the pads jump to.
    const double length = std::max(1.0, song_->lengthBeats);
    const double at = std::clamp(beat_ / length, 0.0, 1.0);
    const int barX = 10, barW = Canvas::width - 20, barY = 194;
    for (int seg = 0; seg < 16; ++seg) {
        const int x0 = barX + seg * barW / 16, x1 = barX + (seg + 1) * barW / 16;
        const bool past = (seg + 1) / 16.0 <= at;
        canvas.fillAlpha({x0, barY, x1 - x0 - 1, 6}, past ? p.accent : p.textDim, past ? 140 : 60);
    }
    canvas.fill({barX + static_cast<int>(at * (barW - 2)), barY - 2, 2, 10}, p.accentHot);
    canvas.textCentered({0, 208, Canvas::width, 12}, "Wheel picks the lead, solo. Pads scrub the song.", p.textDim, Font::Small);
    canvas.textCentered({0, 226, Canvas::width, 12}, "B1 BACK          PLAY OR PRESS TO START", p.textDim, Font::Small);
}

void Game::drawPaused(Canvas& canvas, int64_t nowMs) {
    (void)nowMs;
    const Palette& p = palette_;
    canvas.fillAlpha(kScreen, 0x000000, 150);
    const char* word = "PAUSED";
    canvas.textScaled((Canvas::width - Canvas::scaledWidth(word, 2)) / 2, 86, word, p.white, 2);
    canvas.textCentered({0, 130, Canvas::width, 12}, "PLAY  RESUME      STOP  QUIT", p.textDim, Font::Small);
}

void Game::drawGameOver(Canvas& canvas, int64_t nowMs) {
    const Palette& p = palette_;
    const int64_t age = nowMs - stateSince_;
    canvas.fillAlpha(kScreen, 0x000000, static_cast<int>(std::min<int64_t>(170, age / 3)));
    const bool best = finalRank_ == 0;
    const char* word = songComplete_ ? "SONG CLEAR" : best ? "NEW BEST" : "YOU FAILED";
    const int w = Canvas::scaledWidth(word, 2);
    canvas.glow(160, 70, 70, best || songComplete_ ? p.accent : p.danger, 60);
    canvas.textScaled((Canvas::width - w) / 2 + 2, 56, word, 0x000000, 2, 150);
    canvas.textScaled((Canvas::width - w) / 2, 54, word, best || songComplete_ ? p.accentHot : p.white, 2);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d", score_);
    const int sw = Canvas::scaledWidth(buf, 2);
    canvas.textScaled((Canvas::width - sw) / 2, 92, buf, p.white, 2);
    const int judged = perfects_ + goods_ + misses_;
    std::snprintf(buf, sizeof buf, "%s   LEVEL %d   STREAK %d   %d%% HIT", difficultyName(difficulty_), level_, maxCombo_, judged ? (perfects_ + goods_) * 100 / judged : 0);
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
    const int row = padRow(pad), lane = padCol(pad);
    switch (state_) {
    case State::Playing:
    case State::Paused: {
        // Rows are time: the bottom row is the strike line, the top row three steps ahead.
        const int64_t k = columnBase() + (3 - row);
        if (k < 0) return 0;
        const Slot s = track_.at(k);
        if (!s.cue) return 0;
        const bool lit = lane == laneOf(s.row) || (s.row2 >= 0 && lane == laneOf(s.row2));
        if (lit) {
            float level;
            if (row == 3) level = 0.55f + 0.45f * static_cast<float>(1.0 - stepPhase());
            else level = 0.45f - (3 - row) * 0.1f;
            if (state_ == State::Paused) level *= 0.3f;
            if (twinHalf_ && row == 3 && lane == twinHalfRow_) level = 1.0f;
            return Palette::scale(p.row[lane], level);
        }
        if (s.decoy >= 0 && lane == laneOf(s.decoy)) return row == 3 ? 0x5a0000 : 0x2a0000;
        return 0;
    }
    case State::SongInfo: {
        // A progress bar across the pads: pad 1 the start, pad 16 the end, the current sixteenth bright.
        if (!song_) return 0;
        const double at = std::clamp(beat_ / std::max(1.0, song_->lengthBeats), 0.0, 0.9999);
        const int here = static_cast<int>(at * 16);
        if (pad == here) return Palette::scale(p.accentHot, 0.6f + 0.4f * static_cast<float>(1.0 - beatPhase()));
        return pad < here ? Palette::scale(p.accent, 0.18f) : Palette::scale(p.row[lane], 0.04f);
    }
    case State::GameOver: {
        const float t = 0.5f + 0.5f * std::sin(nowMs * 0.004f + row * 0.6f);
        return Palette::scale(p.danger, 0.08f + 0.25f * t);
    }
    default: {
        const float t = 0.5f + 0.5f * std::sin(titlePhase_ * 2.4f - row * 0.9f + lane * 0.5f);
        return Palette::scale(p.row[lane], 0.05f + 0.45f * t * t);
    }
    }
}

void Game::drawPanelLights(Canvas& canvas, int64_t nowMs) {
    for (int pad = 0; pad < 16; ++pad) canvas.padLed(pad, padColor(pad, nowMs));
    const bool playing = state_ == State::Playing;
    canvas.buttonLed(IN_PLAY, playing ? 2 : 1);
    canvas.buttonLed(IN_STOP, state_ == State::Title ? 0 : 1);
    canvas.buttonLed(IN_SHIFT, 1);
    const double phase = state_ == State::Playing ? beatPhase() : std::fmod(engine_.positionBeats(), 1.0);
    canvas.buttonLed(IN_TAP_TEMPO, phase < 0.12 ? 2 : 0);
    const bool chase = !banner_.empty() && nowMs < bannerUntil_ && banner_.rfind("LEVEL", 0) == 0;
    static const int column[] = {IN_MUTE, IN_ERASE, IN_CHOP, IN_LOOP, IN_SAMPLE_REC, IN_SEQ_REC};
    const int lit = chase ? static_cast<int>((nowMs / 70) % 6) : -1;
    for (int i = 0; i < 6; ++i) canvas.buttonLed(column[i], i == lit ? 2 : 0);
    for (int b : {IN_SEQ, IN_SAMPLE, IN_KNOB_FX, IN_PAD_FX, IN_PAD_BANK, IN_NOTE_REPEAT, IN_LEVELS, IN_SAMPLE_SELECT, IN_MINUS, IN_PLUS})
        canvas.buttonLed(b, 0);
    const int rock = playing || state_ == State::Paused ? static_cast<int>(rock_ * 5.99f) : static_cast<int>(engine_.peakLeft() * 6);
    canvas.meters(rock, static_cast<int>(engine_.kickEnvelope() * 5.99f));
    canvas.trackLed(playing || state_ == State::Paused ? (level_ - 1) % 8 : static_cast<int>((nowMs / 350) % 8));
}

} // namespace lightfoot
