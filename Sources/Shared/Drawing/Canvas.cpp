#include "Shared/Drawing/Canvas.hpp"

#include <algorithm>
#include <ctime>

extern "C" {
#include "font.h"
#include "ui.h"
}

namespace lightfoot {

Canvas::Canvas(ui* u) : u_(u) {}

void Canvas::clear(uint32_t color) {
    std::fill(u_->px, u_->px + width * height, color);
}

void Canvas::pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    u_->px[y * width + x] = color;
}

void Canvas::fill(Rect r, uint32_t color) {
    r = r.intersect(bounds());
    if (r.empty()) return;
    for (int y = r.y; y < r.bottom(); ++y)
        std::fill(u_->px + y * width + r.x, u_->px + y * width + r.right(), color);
}

void Canvas::stroke(Rect r, uint32_t color, int px) {
    if (r.empty() || px <= 0) return;
    px = std::min(px, std::min(r.w, r.h) / 2 + 1);
    fill({r.x, r.y, r.w, px}, color);
    fill({r.x, r.bottom() - px, r.w, px}, color);
    fill({r.x, r.y, px, r.h}, color);
    fill({r.right() - px, r.y, px, r.h}, color);
}

void Canvas::hline(int x0, int x1, int y, uint32_t color) {
    if (x1 < x0) std::swap(x0, x1);
    fill({x0, y, x1 - x0 + 1, 1}, color);
}

void Canvas::vline(int x, int y0, int y1, uint32_t color) {
    if (y1 < y0) std::swap(y0, y1);
    fill({x, y0, 1, y1 - y0 + 1}, color);
}

void Canvas::dim(Rect r, uint32_t color) {
    r = r.intersect(bounds());
    for (int y = r.y; y < r.bottom(); ++y)
        for (int x = r.x + (y & 1); x < r.right(); x += 2)
            u_->px[y * width + x] = color;
}

namespace {
uint32_t lerpColor(uint32_t a, uint32_t b, int t) {
    uint32_t r = (((a >> 16) & 0xff) * (255 - t) + ((b >> 16) & 0xff) * t) / 255;
    uint32_t g = (((a >> 8) & 0xff) * (255 - t) + ((b >> 8) & 0xff) * t) / 255;
    uint32_t bl = ((a & 0xff) * (255 - t) + (b & 0xff) * t) / 255;
    return (r << 16) | (g << 8) | bl;
}
bool cornerPixel(Rect r, int x, int y) {
    const bool left = x == r.x, right = x == r.right() - 1, top = y == r.y, bottom = y == r.bottom() - 1;
    return (left || right) && (top || bottom);
}
} // namespace

void Canvas::gradientV(Rect r, uint32_t top, uint32_t bottom) {
    Rect c = r.intersect(bounds());
    if (c.empty()) return;
    for (int y = c.y; y < c.bottom(); ++y) {
        const int t = r.h <= 1 ? 0 : (y - r.y) * 255 / (r.h - 1);
        std::fill(u_->px + y * width + c.x, u_->px + y * width + c.right(), lerpColor(top, bottom, t));
    }
}

void Canvas::fillRounded(Rect r, uint32_t color) {
    if (r.w < 3 || r.h < 3) {
        fill(r, color);
        return;
    }
    fill({r.x + 1, r.y, r.w - 2, r.h}, color);
    fill({r.x, r.y + 1, r.w, r.h - 2}, color);
}

void Canvas::gradientRounded(Rect r, uint32_t top, uint32_t bottom) {
    if (r.w < 3 || r.h < 3) {
        gradientV(r, top, bottom);
        return;
    }
    Rect c = r.intersect(bounds());
    for (int y = c.y; y < c.bottom(); ++y) {
        const int t = r.h <= 1 ? 0 : (y - r.y) * 255 / (r.h - 1);
        const uint32_t col = lerpColor(top, bottom, t);
        for (int x = c.x; x < c.right(); ++x)
            if (!cornerPixel(r, x, y)) u_->px[y * width + x] = col;
    }
}

void Canvas::strokeRounded(Rect r, uint32_t color) {
    if (r.w < 3 || r.h < 3) {
        stroke(r, color, 1);
        return;
    }
    hline(r.x + 1, r.right() - 2, r.y, color);
    hline(r.x + 1, r.right() - 2, r.bottom() - 1, color);
    vline(r.x, r.y + 1, r.bottom() - 2, color);
    vline(r.right() - 1, r.y + 1, r.bottom() - 2, color);
}

int Canvas::glyphWidth(Font font) {
    switch (font) {
    case Font::Small: return FONT_S_W;
    case Font::Medium: return FONT_L_W;
    case Font::Large: return FONT_L_W * 2;
    }
    return FONT_S_W;
}

int Canvas::fontHeight(Font font) {
    switch (font) {
    case Font::Small: return FONT_S_H;
    case Font::Medium: return FONT_L_H;
    case Font::Large: return FONT_L_H * 2;
    }
    return FONT_S_H;
}

int Canvas::textWidth(std::string_view s, Font font) {
    return static_cast<int>(s.size()) * glyphWidth(font);
}

void Canvas::glyph(int x, int y, Rect clip, char ch, uint32_t color, Font font) {
    int c = static_cast<unsigned char>(ch);
    if (c < 32 || c > 126) c = '?';
    const bool small = font == Font::Small;
    const unsigned char* g = small ? FONT_S[c - 32] : FONT_L[c - 32];
    const int gw = small ? FONT_S_W : FONT_L_W;
    const int gh = small ? FONT_S_H : FONT_L_H;
    const int scale = font == Font::Large ? 2 : 1;
    for (int r = 0; r < gh; ++r) {
        for (int col = 0; col < gw; ++col) {
            if (!(g[r] & (0x80 >> col))) continue;
            for (int sy = 0; sy < scale; ++sy)
                for (int sx = 0; sx < scale; ++sx) {
                    int px = x + col * scale + sx, py = y + r * scale + sy;
                    if (clip.contains(px, py)) u_->px[py * width + px] = color;
                }
        }
    }
}

int Canvas::text(int x, int y, std::string_view s, uint32_t color, Font font) {
    return textClipped(bounds(), x, y, s, color, font);
}

int Canvas::textClipped(Rect clip, int x, int y, std::string_view s, uint32_t color, Font font) {
    clip = clip.intersect(bounds());
    const int gw = glyphWidth(font);
    for (char ch : s) {
        if (x >= clip.right()) break;
        if (x + gw > clip.x) glyph(x, y, clip, ch, color, font);
        x += gw;
    }
    return x;
}

void Canvas::textCentered(Rect r, std::string_view s, uint32_t color, Font font) {
    int tw = textWidth(s, font), th = fontHeight(font);
    textClipped(r, r.x + (r.w - tw) / 2, r.y + (r.h - th) / 2, s, color, font);
}

void Canvas::textRight(Rect r, std::string_view s, uint32_t color, Font font) {
    int tw = textWidth(s, font), th = fontHeight(font);
    textClipped(r, r.right() - tw, r.y + (r.h - th) / 2, s, color, font);
}

void Canvas::padLed(int pad, uint32_t rgb) {
    ui_led(u_, pad, static_cast<int>((rgb >> 17) & 0x7f), static_cast<int>((rgb >> 9) & 0x7f), static_cast<int>((rgb >> 1) & 0x7f));
}

void Canvas::buttonLed(int button, int level) {
    ui_button_led(u_, button, level);
}

void Canvas::wake(int ms) {
    ui_wake(u_, ms);
}

void Canvas::quit() {
    ui_quit(u_);
}

int64_t Canvas::nowMs() const {
    if (u_->now_ms) return u_->now_ms;
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<int64_t>(t.tv_sec) * 1000 + t.tv_nsec / 1000000;
}

void Canvas::textScaled(int x, int y, std::string_view s, uint32_t color, int scale, int alpha) {
    if (scale < 1) scale = 1;
    for (char ch : s) {
        int c = static_cast<unsigned char>(ch);
        if (c < 32 || c > 126) c = '?';
        const unsigned char* g = FONT_L[c - 32];
        for (int r = 0; r < FONT_L_H; ++r)
            for (int col = 0; col < FONT_L_W; ++col) {
                if (!(g[r] & (0x80 >> col))) continue;
                if (alpha >= 255) fill({x + col * scale, y + r * scale, scale, scale}, color);
                else fillAlpha({x + col * scale, y + r * scale, scale, scale}, color, alpha);
            }
        x += FONT_L_W * scale;
    }
}

void Canvas::blend(int x, int y, uint32_t color, int alpha) {
    if (x < 0 || y < 0 || x >= width || y >= height || alpha <= 0) return;
    if (alpha >= 255) {
        u_->px[y * width + x] = color;
        return;
    }
    uint32_t& d = u_->px[y * width + x];
    const uint32_t dr = (d >> 16) & 0xff, dg = (d >> 8) & 0xff, db = d & 0xff;
    const uint32_t sr = (color >> 16) & 0xff, sg = (color >> 8) & 0xff, sb = color & 0xff;
    const uint32_t a = static_cast<uint32_t>(alpha), ia = 255 - a;
    d = (((sr * a + dr * ia) / 255) << 16) | (((sg * a + dg * ia) / 255) << 8) | ((sb * a + db * ia) / 255);
}

void Canvas::fillAlpha(Rect r, uint32_t color, int alpha) {
    r = r.intersect(bounds());
    if (r.empty()) return;
    if (alpha >= 255) {
        fill(r, color);
        return;
    }
    for (int y = r.y; y < r.bottom(); ++y)
        for (int x = r.x; x < r.right(); ++x) blend(x, y, color, alpha);
}

void Canvas::disc(int cx, int cy, int radius, uint32_t color) {
    discAlpha(cx, cy, radius, color, 255);
}

void Canvas::discAlpha(int cx, int cy, int radius, uint32_t color, int alpha) {
    if (radius <= 0) return;
    const int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        const int y = cy + dy;
        if (y < 0 || y >= height) continue;
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy > r2) continue;
            blend(cx + dx, y, color, alpha);
        }
    }
}

void Canvas::ring(int cx, int cy, int radius, int thickness, uint32_t color) {
    if (radius <= 0) return;
    const int outer = radius * radius;
    const int innerR = std::max(0, radius - thickness);
    const int inner = innerR * innerR;
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            const int d = dx * dx + dy * dy;
            if (d > outer || d < inner) continue;
            pixel(cx + dx, cy + dy, color);
        }
}

void Canvas::line(int x0, int y0, int x1, int y1, uint32_t color) {
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Canvas::glow(int cx, int cy, int radius, uint32_t color, int alpha) {
    if (radius <= 0 || alpha <= 0) return;
    const int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        const int y = cy + dy;
        if (y < 0 || y >= height) continue;
        for (int dx = -radius; dx <= radius; ++dx) {
            const int d = dx * dx + dy * dy;
            if (d > r2) continue;
            // Quadratic falloff reads as light rather than a flat disc.
            const int a = alpha * (r2 - d) / r2;
            blend(cx + dx, y, color, a * a / std::max(1, alpha));
        }
    }
}

void Canvas::meters(int left, int right) {
    const int l = std::clamp(left, 0, 5), r = std::clamp(right, 0, 5);
    ui_meter(u_, (1 << l) - 1, (1 << r) - 1);
}

void Canvas::trackLed(int index) {
    ui_track_led(u_, index);
}

} // namespace lightfoot
