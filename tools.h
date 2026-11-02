#pragma once

#include "canvas.h"

namespace xphoton {

enum Tool
{
    PENCIL,
    BRUSH,
    ERASER,
    FILL,
    PICKER,
    LINE,
    RECTANGLE,
    ELLIPSE,
    SELECT,
    PAN,
    TOOL_COUNT
};
enum ShapeStyle
{
    OUTLINE,
    SOLID,
    OUTLINE_FILL
};
enum Transform
{
    ROTATE_RIGHT,
    ROTATE_LEFT,
    FLIP_HORIZONTAL,
    FLIP_VERTICAL
};

void  draw_stroke(Canvas &canvas, Point from, Point to, Pixel color, int size);
void  draw_shape(Canvas &canvas, Tool tool, Point from, Point to, Pixel primary, Pixel secondary, int size,
                 ShapeStyle style);
Point constrain_shape(Point start, Point end, Tool tool);
bool  flood_fill(Canvas &canvas, Point point, Pixel color, Rect *changed);
bool  copy_region(const Canvas &source, Rect rect, Canvas &target);
void  paste_region(Canvas &target, const Canvas &source, int x, int y);
bool  resize_image(Canvas &canvas, int width, int height, bool scale, Pixel background);
bool  transform_image(Canvas &canvas, Transform transform);

} // namespace xphoton
