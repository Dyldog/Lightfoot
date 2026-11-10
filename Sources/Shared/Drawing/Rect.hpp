#pragma once

namespace lightfoot {

/// Integer rectangle in screen pixels, origin top-left.
struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    constexpr int right() const { return x + w; }
    constexpr int bottom() const { return y + h; }
    constexpr bool empty() const { return w <= 0 || h <= 0; }
    constexpr bool contains(int px, int py) const { return px >= x && py >= y && px < right() && py < bottom(); }

    constexpr Rect inset(int d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    constexpr Rect inset(int dx, int dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    constexpr Rect offset(int dx, int dy) const { return {x + dx, y + dy, w, h}; }

    /// Intersection, or an empty rect when they do not overlap.
    constexpr Rect intersect(Rect o) const {
        int x0 = x > o.x ? x : o.x;
        int y0 = y > o.y ? y : o.y;
        int x1 = right() < o.right() ? right() : o.right();
        int y1 = bottom() < o.bottom() ? bottom() : o.bottom();
        if (x1 <= x0 || y1 <= y0) return {};
        return {x0, y0, x1 - x0, y1 - y0};
    }

    /// Splits off `size` pixels from the top, returning the top part and shrinking this rect.
    Rect takeTop(int size) {
        Rect r{x, y, w, size};
        y += size;
        h -= size;
        return r;
    }
    Rect takeBottom(int size) {
        h -= size;
        return {x, y + h, w, size};
    }
    Rect takeLeft(int size) {
        Rect r{x, y, size, h};
        x += size;
        w -= size;
        return r;
    }
    Rect takeRight(int size) {
        w -= size;
        return {x + w, y, size, h};
    }
};

} // namespace lightfoot
