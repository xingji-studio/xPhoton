#pragma once

#include <stdint.h>
#include <stdlib.h>

namespace xphoton {

// Pixels use the XAPI RGBA byte layout on x86_64, independently of UI colors.
typedef uint32_t Pixel;

inline Pixel rgba(unsigned r, unsigned g, unsigned b, unsigned a = 255)
{
    return (r & 255) | ((g & 255) << 8) | ((b & 255) << 16) | ((a & 255) << 24);
}

inline int minimum(int a, int b)
{
    return a < b ? a : b;
}
inline int maximum(int a, int b)
{
    return a > b ? a : b;
}
inline int clamp(int v, int lo, int hi)
{
    return maximum(lo, minimum(v, hi));
}

struct Point
{
    int x, y;
};

struct Rect
{
    int  x, y, w, h;
    bool empty() const
    {
        return w <= 0 || h <= 0;
    }
    bool contains(int px, int py) const
    {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

Rect intersect(Rect a, Rect b);
Rect unite(Rect a, Rect b);
Rect between(Point a, Point b);

// Bound dimensions and allocation products before decoding or allocating images.
static const int      MAX_SIDE   = 8192;
static const unsigned MAX_PIXELS = 8 * 1024 * 1024;

class Canvas {
  public:
    Pixel *pixels;
    int    width, height;

    Canvas();
    ~Canvas();
    static bool valid_size(int w, int h);
    bool        create(int w, int h, Pixel background = 0xffffffffU);
    bool        copy(const Canvas &source);
    void        swap(Canvas &other);
    void        reset();
    Rect        bounds() const
    {
        return {0, 0, width, height};
    }
    Pixel get(int x, int y) const;
    void  set(int x, int y, Pixel color);
    void  fill(Rect rect, Pixel color);

  private:
    Canvas(const Canvas &);
    Canvas &operator=(const Canvas &);
};

// Scroll offsets are in display pixels; the image always keeps its native size.
struct Viewport
{
    Rect area;
    int  zoom;
    int  scroll_x, scroll_y;

    void  constrain(const Canvas &canvas);
    Point origin(const Canvas &canvas) const;
    Point image_point(const Canvas &canvas, int x, int y) const;
    Rect  screen_rect(const Canvas &canvas, Rect image_rect) const;
    void  zoom_at(const Canvas &canvas, int percent, int x, int y);
    void  fit(const Canvas &canvas);
};

} // namespace xphoton
