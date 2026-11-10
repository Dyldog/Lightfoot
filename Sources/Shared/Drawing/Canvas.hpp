#pragma once

#include <cstdint>
#include <string_view>

#include "Shared/Drawing/Rect.hpp"

struct ui;

namespace lightfoot {

/// Bitmap fonts shipped with the toolkit. Small is 6x12, Medium 8x16, Large 16x32.
enum class Font { Small, Medium, Large };

/// Thin drawing layer over the toolkit's 320x240 framebuffer. Every call clips
/// to the screen, so callers can draw partially off-screen clips safely.
class Canvas {
public:
    static constexpr int width = 320;
    static constexpr int height = 240;

    explicit Canvas(ui* u);

    ui* raw() const { return u_; }
    Rect bounds() const { return {0, 0, width, height}; }

    void clear(uint32_t color);
    void pixel(int x, int y, uint32_t color);
    void fill(Rect r, uint32_t color);
    void stroke(Rect r, uint32_t color, int px = 1);
    void hline(int x0, int x1, int y, uint32_t color);
    void vline(int x, int y0, int y1, uint32_t color);
    /// Fills a rect with a 50% checker dim, used for muted lanes.
    void dim(Rect r, uint32_t color);
    /// Vertical gradient from `top` to `bottom`.
    void gradientV(Rect r, uint32_t top, uint32_t bottom);
    /// Fill and outline with the corner pixels knocked out, which reads as a 2 px radius at this scale.
    void fillRounded(Rect r, uint32_t color);
    void gradientRounded(Rect r, uint32_t top, uint32_t bottom);
    void strokeRounded(Rect r, uint32_t color);

    /// Draws text and returns the x after the last glyph.
    int text(int x, int y, std::string_view s, uint32_t color, Font font = Font::Small);
    /// Draws text clipped to `clip`.
    int textClipped(Rect clip, int x, int y, std::string_view s, uint32_t color, Font font = Font::Small);
    void textCentered(Rect r, std::string_view s, uint32_t color, Font font = Font::Small);
    void textRight(Rect r, std::string_view s, uint32_t color, Font font = Font::Small);

    static int textWidth(std::string_view s, Font font);
    static int fontHeight(Font font);
    static int glyphWidth(Font font);

    // Panel feedback, queued for the frame loop.
    void padLed(int pad, uint32_t rgb);
    void buttonLed(int button, int level);
    void wake(int ms);
    void quit();
    int64_t nowMs() const;

    /// The large font at an integer scale, translucent: logos and big numbers.
    void textScaled(int x, int y, std::string_view s, uint32_t color, int scale, int alpha = 255);
    static int scaledWidth(std::string_view s, int scale) { return static_cast<int>(s.size()) * 8 * scale; }
    static int scaledHeight(int scale) { return 16 * scale; }

    // Game drawing: translucency, circles, lines and the panel's extra lights.
    void blend(int x, int y, uint32_t color, int alpha);              // alpha 0..255
    void fillAlpha(Rect r, uint32_t color, int alpha);
    void disc(int cx, int cy, int radius, uint32_t color);
    void discAlpha(int cx, int cy, int radius, uint32_t color, int alpha);
    void ring(int cx, int cy, int radius, int thickness, uint32_t color);
    void line(int x0, int y0, int x1, int y1, uint32_t color);
    /// A soft glow: concentric translucent discs fading out to `radius`.
    void glow(int cx, int cy, int radius, uint32_t color, int alpha);
    /// The two 5-LED level meters right of the screen, 0..5 lit each.
    void meters(int left, int right);
    /// Track LED A..H (0..7) left of the screen, -1 for none.
    void trackLed(int index);

private:
    void glyph(int x, int y, Rect clip, char ch, uint32_t color, Font font);

    ui* u_;
};

} // namespace lightfoot
