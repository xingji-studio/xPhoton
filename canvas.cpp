#include "canvas.h"
#include <stdlib.h>
#include <string.h>

namespace xphoton {

Rect intersect(Rect a, Rect b)
{
    int x = maximum(a.x, b.x), y = maximum(a.y, b.y);
    return {x, y, maximum(0, minimum(a.x + a.w, b.x + b.w) - x), maximum(0, minimum(a.y + a.h, b.y + b.h) - y)};
}

Rect unite(Rect a, Rect b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    int x = minimum(a.x, b.x), y = minimum(a.y, b.y);
    return {x, y, maximum(a.x + a.w, b.x + b.w) - x, maximum(a.y + a.h, b.y + b.h) - y};
}

Rect between(Point a, Point b)
{
    return {minimum(a.x, b.x), minimum(a.y, b.y), maximum(a.x, b.x) - minimum(a.x, b.x) + 1,
            maximum(a.y, b.y) - minimum(a.y, b.y) + 1};
}

Canvas::Canvas() : pixels(NULL), width(0), height(0) {}

Canvas::~Canvas()
{
    reset();
}

bool Canvas::valid_size(int w, int h)
{
    return w > 0 && h > 0 && w <= MAX_SIDE && h <= MAX_SIDE && (uint64_t)w * h <= MAX_PIXELS;
}

bool Canvas::create(int w, int h, Pixel background)
{
    if (!valid_size(w, h)) return false;
    Pixel *data = (Pixel *)malloc((size_t)w * h * sizeof(Pixel));
    if (!data) return false;
    for (size_t i = 0; i < (size_t)w * h; ++i)
        data[i] = background;
    free(pixels);
    pixels = data;
    width  = w;
    height = h;
    return true;
}

bool Canvas::copy(const Canvas &source)
{
    if (this == &source) return true;
    if (!source.pixels) return false;
    if (width != source.width || height != source.height)
    {
        if (!create(source.width, source.height)) return false;
    }
    memcpy(pixels, source.pixels, (size_t)width * height * sizeof(Pixel));
    return true;
}

void Canvas::swap(Canvas &other)
{
    Pixel *data = pixels;
    int    w = width, h = height;
    pixels       = other.pixels;
    width        = other.width;
    height       = other.height;
    other.pixels = data;
    other.width  = w;
    other.height = h;
}

void Canvas::reset()
{
    free(pixels);
    pixels = NULL;
    width = height = 0;
}

Pixel Canvas::get(int x, int y) const
{
    return bounds().contains(x, y) ? pixels[(size_t)y * width + x] : 0;
}

void Canvas::set(int x, int y, Pixel color)
{
    if (bounds().contains(x, y)) pixels[(size_t)y * width + x] = color;
}

void Canvas::fill(Rect rect, Pixel color)
{
    rect = intersect(rect, bounds());
    for (int y = rect.y; y < rect.y + rect.h; ++y)
        for (int x = rect.x; x < rect.x + rect.w; ++x)
            pixels[(size_t)y * width + x] = color;
}

void Viewport::constrain(const Canvas &canvas)
{
    zoom     = clamp(zoom, 25, 800);
    scroll_x = clamp(scroll_x, 0, maximum(0, (canvas.width * zoom + 99) / 100 - area.w));
    scroll_y = clamp(scroll_y, 0, maximum(0, (canvas.height * zoom + 99) / 100 - area.h));
}

Point Viewport::origin(const Canvas &canvas) const
{
    int w = (canvas.width * zoom + 99) / 100, h = (canvas.height * zoom + 99) / 100;
    return {area.x + maximum(0, (area.w - w) / 2) - scroll_x, area.y + maximum(0, (area.h - h) / 2) - scroll_y};
}

static int floor_div(int value, int divisor)
{
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

Point Viewport::image_point(const Canvas &canvas, int x, int y) const
{
    Point offset = origin(canvas);
    return {floor_div((x - offset.x) * 100, zoom), floor_div((y - offset.y) * 100, zoom)};
}

Rect Viewport::screen_rect(const Canvas &canvas, Rect image_rect) const
{
    Point offset = origin(canvas);
    int   x1 = floor_div(image_rect.x * zoom, 100), y1 = floor_div(image_rect.y * zoom, 100);
    int   x2 = ((image_rect.x + image_rect.w) * zoom + 99) / 100;
    int   y2 = ((image_rect.y + image_rect.h) * zoom + 99) / 100;
    return {offset.x + x1, offset.y + y1, x2 - x1, y2 - y1};
}

void Viewport::zoom_at(const Canvas &canvas, int percent, int x, int y)
{
    Point   offset   = origin(canvas);
    int     old_zoom = zoom;
    int64_t anchor_x = (int64_t)(x - offset.x) * 10000 / old_zoom;
    int64_t anchor_y = (int64_t)(y - offset.y) * 10000 / old_zoom;
    zoom             = clamp(percent, 25, 800);
    int w = (canvas.width * zoom + 99) / 100, h = (canvas.height * zoom + 99) / 100;
    scroll_x = area.x + maximum(0, (area.w - w) / 2) + (int)(anchor_x * zoom / 10000) - x;
    scroll_y = area.y + maximum(0, (area.h - h) / 2) + (int)(anchor_y * zoom / 10000) - y;
    constrain(canvas);
}

void Viewport::fit(const Canvas &canvas)
{
    if (canvas.width <= 0 || canvas.height <= 0) return;
    zoom = clamp(minimum(maximum(1, area.w - 24) * 100 / canvas.width, maximum(1, area.h - 24) * 100 / canvas.height),
                 25, 800);
    scroll_x = scroll_y = 0;
    constrain(canvas);
}

} // namespace xphoton
