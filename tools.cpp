#include "tools.h"
#include <stdlib.h>
#include <string.h>

namespace xphoton {

static void stamp(Canvas &canvas, Point point, Pixel color, int size)
{
    int  left = point.x - (size - 1) / 2, top = point.y - (size - 1) / 2;
    Rect rect = intersect({left, top, size, size}, canvas.bounds());
    for (int y = rect.y; y < rect.y + rect.h; ++y)
        for (int x = rect.x; x < rect.x + rect.w; ++x)
        {
            int dx = 2 * (x - left) - (size - 1), dy = 2 * (y - top) - (size - 1);
            if (dx * dx + dy * dy <= size * size) canvas.set(x, y, color);
        }
}

void draw_stroke(Canvas &canvas, Point from, Point to, Pixel color, int size)
{
    size = clamp(size, 1, 64);
    // GUI drags are clamped before this call. Bound public inputs as well, so a
    // lost/outside mouse coordinate cannot cause a billion-iteration line.
    from.x = clamp(from.x, -MAX_SIDE, MAX_SIDE * 2);
    from.y = clamp(from.y, -MAX_SIDE, MAX_SIDE * 2);
    to.x   = clamp(to.x, -MAX_SIDE, MAX_SIDE * 2);
    to.y   = clamp(to.y, -MAX_SIDE, MAX_SIDE * 2);
    int dx = abs(to.x - from.x), sx = from.x < to.x ? 1 : -1;
    int dy = -abs(to.y - from.y), sy = from.y < to.y ? 1 : -1;
    int error = dx + dy;
    for (;;)
    {
        stamp(canvas, from, color, size);
        if (from.x == to.x && from.y == to.y) break;
        int twice = 2 * error;
        if (twice >= dy)
        {
            error  += dy;
            from.x += sx;
        }
        if (twice <= dx)
        {
            error  += dx;
            from.y += sy;
        }
    }
}

Point constrain_shape(Point start, Point end, Tool tool)
{
    int dx = end.x - start.x, dy = end.y - start.y;
    int ax = abs(dx), ay = abs(dy);
    if (tool == LINE && ax > 2 * ay)
        end.y = start.y;
    else if (tool == LINE && ay > 2 * ax)
        end.x = start.x;
    else
    {
        int side = maximum(ax, ay);
        end.x    = start.x + (dx < 0 ? -side : side);
        end.y    = start.y + (dy < 0 ? -side : side);
    }
    return end;
}

void draw_shape(Canvas &canvas, Tool tool, Point from, Point to, Pixel primary, Pixel secondary, int size,
                ShapeStyle style)
{
    size = clamp(size, 1, 64);
    if (tool == LINE)
    {
        draw_stroke(canvas, from, to, primary, size);
        return;
    }
    Rect box     = between(from, to);
    Rect visible = intersect(box, canvas.bounds());
    if (tool == RECTANGLE)
    {
        if (style != OUTLINE) canvas.fill(box, style == SOLID ? primary : secondary);
        if (style != SOLID)
        {
            canvas.fill({box.x, box.y, box.w, minimum(size, box.h)}, primary);
            canvas.fill({box.x, box.y + maximum(0, box.h - size), box.w, minimum(size, box.h)}, primary);
            canvas.fill({box.x, box.y, minimum(size, box.w), box.h}, primary);
            canvas.fill({box.x + maximum(0, box.w - size), box.y, minimum(size, box.w), box.h}, primary);
        }
        return;
    }
    if (tool != ELLIPSE) return;
    int64_t w2 = (int64_t)box.w * box.w, h2 = (int64_t)box.h * box.h;
    int     iw = box.w - 2 * size, ih = box.h - 2 * size;
    int64_t iw2 = (int64_t)maximum(0, iw) * maximum(0, iw);
    int64_t ih2 = (int64_t)maximum(0, ih) * maximum(0, ih);
    for (int y = visible.y; y < visible.y + visible.h; ++y)
        for (int x = visible.x; x < visible.x + visible.w; ++x)
        {
            int64_t dx = 2 * (x - box.x) - (box.w - 1), dy = 2 * (y - box.y) - (box.h - 1);
            if (dx * dx * h2 + dy * dy * w2 > w2 * h2) continue;
            bool inside = iw > 0 && ih > 0 && dx * dx * ih2 + dy * dy * iw2 <= iw2 * ih2;
            if (style == SOLID || !inside)
                canvas.set(x, y, primary);
            else if (style == OUTLINE_FILL)
                canvas.set(x, y, secondary);
        }
}

bool flood_fill(Canvas &canvas, Point point, Pixel color, Rect *changed)
{
    if (changed) *changed = {0, 0, 0, 0};
    if (!canvas.bounds().contains(point.x, point.y)) return true;
    Pixel original = canvas.get(point.x, point.y);
    if (original == color) return true;
    size_t capacity = 256, length = 1;
    Point *stack = (Point *)malloc(capacity * sizeof(Point));
    if (!stack) return false;
    stack[0]   = point;
    Rect dirty = {0, 0, 0, 0};
    // Scanline fill uses a heap stack instead of recursing through image pixels.
    while (length)
    {
        Point seed = stack[--length];
        if (canvas.get(seed.x, seed.y) != original) continue;
        int left = seed.x, right = seed.x;
        while (left > 0 && canvas.get(left - 1, seed.y) == original)
            --left;
        while (right + 1 < canvas.width && canvas.get(right + 1, seed.y) == original)
            ++right;
        canvas.fill({left, seed.y, right - left + 1, 1}, color);
        dirty = unite(dirty, {left, seed.y, right - left + 1, 1});
        for (int step = -1; step <= 1; step += 2)
        {
            int y = seed.y + step;
            if (y < 0 || y >= canvas.height) continue;
            bool run = false;
            for (int x = left; x <= right; ++x)
            {
                bool match = canvas.get(x, y) == original;
                if (match && !run)
                {
                    if (length == capacity)
                    {
                        size_t next = capacity * 2;
                        if (next > MAX_PIXELS) next = MAX_PIXELS;
                        Point *grown = next > capacity ? (Point *)realloc(stack, next * sizeof(Point)) : NULL;
                        if (!grown)
                        {
                            free(stack);
                            return false;
                        }
                        stack    = grown;
                        capacity = next;
                    }
                    stack[length++] = {x, y};
                }
                run = match;
            }
        }
    }
    free(stack);
    if (changed) *changed = dirty;
    return true;
}

bool copy_region(const Canvas &source, Rect rect, Canvas &target)
{
    rect = intersect(rect, source.bounds());
    if (rect.empty()) return false;
    Canvas result;
    if (!result.create(rect.w, rect.h)) return false;
    for (int y = 0; y < rect.h; ++y)
        memcpy(result.pixels + (size_t)y * rect.w, source.pixels + (size_t)(rect.y + y) * source.width + rect.x,
               (size_t)rect.w * sizeof(Pixel));
    target.swap(result);
    return true;
}

void paste_region(Canvas &target, const Canvas &source, int x, int y)
{
    Rect visible = intersect({x, y, source.width, source.height}, target.bounds());
    for (int row = visible.y; row < visible.y + visible.h; ++row)
        memcpy(target.pixels + (size_t)row * target.width + visible.x,
               source.pixels + (size_t)(row - y) * source.width + visible.x - x, (size_t)visible.w * sizeof(Pixel));
}

bool resize_image(Canvas &canvas, int width, int height, bool scale, Pixel background)
{
    Canvas result;
    if (!result.create(width, height, background)) return false;
    if (!scale)
        paste_region(result, canvas, 0, 0);
    else
    {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                result.pixels[(size_t)y * width + x] =
                    canvas.get((int)((int64_t)x * canvas.width / width), (int)((int64_t)y * canvas.height / height));
    }
    result.swap(canvas);
    return true;
}

bool transform_image(Canvas &canvas, Transform transform)
{
    Canvas result;
    bool   rotate = transform == ROTATE_RIGHT || transform == ROTATE_LEFT;
    if (!result.create(rotate ? canvas.height : canvas.width, rotate ? canvas.width : canvas.height)) return false;
    for (int y = 0; y < canvas.height; ++y)
        for (int x = 0; x < canvas.width; ++x)
        {
            int dx = x, dy = y;
            if (transform == ROTATE_RIGHT)
            {
                dx = canvas.height - 1 - y;
                dy = x;
            }
            if (transform == ROTATE_LEFT)
            {
                dx = y;
                dy = canvas.width - 1 - x;
            }
            if (transform == FLIP_HORIZONTAL) dx = canvas.width - 1 - x;
            if (transform == FLIP_VERTICAL) dy = canvas.height - 1 - y;
            result.set(dx, dy, canvas.get(x, y));
        }
    result.swap(canvas);
    return true;
}

} // namespace xphoton
