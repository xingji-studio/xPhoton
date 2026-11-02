#include <xtext_layout.h>
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace xphoton {

extern const Pixel PALETTE[24] = {0xff000000, 0xff808080, 0xffc0c0c0, 0xffffffff, 0xff3636e0, 0xff286cff,
                                  0xff28b4ff, 0xff50e2ff, 0xff38ac28, 0xff91cf38, 0xffdedb22, 0xffdbb034,
                                  0xffe57e21, 0xffa75563, 0xffcb48a3, 0xffb580e9, 0xff492a7a, 0xff7597c7,
                                  0xffaac7ed, 0xffcaf4ff, 0xffc4eed2, 0xffefe1c6, 0xffefc8db, 0xffd8c2ef};

static const UINT32 BG = 0xf4f7fbff, PANEL = 0xffffffff, BORDER = 0xd6dfeaff, TEXT = 0x243247ff;
static const UINT32 MUTED = 0x64748bff, ACCENT = 0x176bd6ff, SELECTED = 0xe3efffff;

static UINT32 ui_color(Pixel color)
{
    return ((color & 255) << 24) | ((color & 0xff00) << 8) | ((color & 0xff0000) >> 8) | 255;
}

static void fill(App &app, Rect rect, UINT32 color)
{
    rect = intersect(rect, {0, 0, app.width, app.height});
    if (!rect.empty()) xapi_DrawRect(app.window, rect.x, rect.y, rect.x + rect.w - 1, rect.y + rect.h - 1, color, true);
}

static void outline(App &app, Rect rect, UINT32 color)
{
    rect = intersect(rect, {0, 0, app.width, app.height});
    if (!rect.empty())
        xapi_DrawRect(app.window, rect.x, rect.y, rect.x + rect.w - 1, rect.y + rect.h - 1, color, false);
}

static void text(App &app, int x, int y, const char *value, UINT32 color = TEXT, int size = 13, int available = -1)
{
    if (x < 0 || y < 0 || y + size > app.height || !value) return;
    available = minimum(available < 0 ? app.width - x : available, app.width - x);
    if (available <= 0) return;
    if ((int)xapi_CalcTextWidth((char *)value, size) <= available)
    {
        xapi_DrawTextTop(app.window, x, y, (char *)value, size, color);
        return;
    }
    char   shortened[256];
    size_t length = minimum((int)strlen(value), (int)sizeof(shortened) - 4);
    memcpy(shortened, value, length);
    shortened[length] = '\0';
    while (length)
    {
        // Remove whole UTF-8 code points before appending an ellipsis.
        --length;
        while (length && (((unsigned char)shortened[length] & 0xc0) == 0x80))
            --length;
        memcpy(shortened + length, "...", 4);
        if ((int)xapi_CalcTextWidth(shortened, size) <= available)
        {
            xapi_DrawTextTop(app.window, x, y, shortened, size, color);
            return;
        }
    }
}

const char *tool_name(const App &app, Tool tool)
{
    static const char *zh[TOOL_COUNT] = {"铅笔", "画笔", "橡皮", "填充", "吸管",
                                         "直线", "矩形", "椭圆", "选择", "平移"};
    static const char *en[TOOL_COUNT] = {"Pencil", "Brush",     "Eraser",  "Fill",   "Picker",
                                         "Line",   "Rectangle", "Ellipse", "Select", "Pan"};
    return app.tr(zh[tool], en[tool]);
}

static bool enabled(const App &app, int id)
{
    if (id == CMD_UNDO) return app.history.can_undo();
    if (id == CMD_REDO) return app.history.can_redo();
    if (id == CMD_CROP || id == CMD_CUT || id == CMD_COPY || id == CMD_DELETE || id == CMD_DESELECT)
        return !app.selection.empty();
    if (id == CMD_PASTE)
    {
        XClipInfo info;
        if (xapi_ClipboardQuery(&info) == 0)
            for (unsigned i = 0; i < info.count; ++i)
                if (info.entries[i].format == XCLIP_RGBA8) return true;
        return false;
    }
    return true;
}

static void control(App &app, int id, Rect rect, const char *zh, const char *en, const char *icon = NULL)
{
    if (app.control_count == 128) return;
    bool selected                     = id >= CMD_TOOL && id < CMD_TOOL + TOOL_COUNT && app.tool == id - CMD_TOOL;
    app.controls[app.control_count++] = {id, rect, zh, en, icon, enabled(app, id), selected};
}

struct Flow
{
    App &app;
    int  x, y, row_height;

    void add(int id, int w, const char *zh, const char *en, const char *icon = NULL)
    {
        w = minimum(w, maximum(1, app.width - 16));
        if (x + w > app.width - 8 && x > 8)
        {
            x  = 8;
            y += row_height + 4;
        }
        control(app, id, {x, y, w, row_height}, zh, en, icon);
        x += w + 4;
    }
};

static Rect dialog_rect(const App &app)
{
    int w = minimum(476, maximum(1, app.width - 16));
    int h = app.modal == MODAL_ABOUT ? 330 : (app.modal == MODAL_UNSAVED || app.modal == MODAL_OVERWRITE) ? 210 : 254;
    h     = minimum(h, maximum(1, app.height - 16));
    return {maximum(0, (app.width - w) / 2), maximum(0, (app.height - h) / 2), w, h};
}

static int modal_field_offset(const App &app)
{
    const char *first = app.modal == MODAL_COLOR ? "Hex RGB #" : app.tr("宽度（像素）", "Width (px)");
    const char *second = app.tr("高度（像素）", "Height (px)");
    int label_width = (int)xapi_CalcTextWidth((char *)first, 13);
    if (app.modal != MODAL_COLOR)
        label_width = maximum(label_width, (int)xapi_CalcTextWidth((char *)second, 13));
    return minimum(maximum(128, label_width + 32), maximum(40, dialog_rect(app).w - 96));
}

struct MenuEntry
{
    int         id;
    const char *zh, *en;
};

static const MenuEntry FILE_MENU[] = {
    {CMD_NEW,     "新建                   Ctrl+N",  "New                    Ctrl+N" },
    {CMD_OPEN,    "打开…                 Ctrl+O", "Open...                Ctrl+O" },
    {CMD_SAVE,    "保存                   Ctrl+S",  "Save                   Ctrl+S" },
    {CMD_SAVE_AS, "另存为…      Ctrl+Shift+S",   "Save As...        Ctrl+Shift+S"},
    {CMD_EXIT,    "退出",                           "Exit"                          }
};
static const MenuEntry EDIT_MENU[] = {
    {CMD_UNDO,       "撤销                   Ctrl+Z",   "Undo                   Ctrl+Z"},
    {CMD_REDO,       "重做                   Ctrl+Y",   "Redo                   Ctrl+Y"},
    {CMD_CUT,        "剪切                   Ctrl+X",   "Cut                    Ctrl+X"},
    {CMD_COPY,       "复制                   Ctrl+C",   "Copy                   Ctrl+C"},
    {CMD_PASTE,      "粘贴                   Ctrl+V",   "Paste                  Ctrl+V"},
    {CMD_DELETE,     "删除选区                  Del", "Delete selection          Del"},
    {CMD_SELECT_ALL, "全选                   Ctrl+A",   "Select All             Ctrl+A"},
    {CMD_DESELECT,   "取消选择                  Esc", "Deselect                  Esc"}
};
static const MenuEntry VIEW_MENU[] = {
    {CMD_ZOOM_IN,     "放大                   Ctrl++",   "Zoom In                Ctrl++"},
    {CMD_ZOOM_OUT,    "缩小                   Ctrl+-",   "Zoom Out               Ctrl+-"},
    {CMD_ACTUAL_SIZE, "实际大小（100%）",            "Actual Size (100%)"           },
    {CMD_FIT,         "适应窗口               Ctrl+0", "Fit to Window          Ctrl+0"}
};
static const MenuEntry IMAGE_MENU[] = {
    {CMD_CROP,          "裁剪到选区",      "Crop to Selection"      },
    {CMD_RESIZE_CANVAS, "画布尺寸…",      "Canvas Size..."         },
    {CMD_RESIZE_IMAGE,  "缩放图像…",      "Resize Image..."        },
    {CMD_ROTATE_RIGHT,  "顺时针旋转 90°", "Rotate 90 degrees Right"},
    {CMD_ROTATE_LEFT,   "逆时针旋转 90°", "Rotate 90 degrees Left" },
    {CMD_FLIP_H,        "水平翻转",         "Flip Horizontal"        },
    {CMD_FLIP_V,        "垂直翻转",         "Flip Vertical"          }
};
static const MenuEntry HELP_MENU[] = {
    {CMD_ABOUT, "关于与快捷键              F1", "About & Shortcuts          F1"}
};

static void menu_entries(int index, const MenuEntry **entries, int *count)
{
    switch (index)
    {
    case 0:
        *entries = FILE_MENU;
        *count   = sizeof(FILE_MENU) / sizeof(FILE_MENU[0]);
        break;
    case 1:
        *entries = EDIT_MENU;
        *count   = sizeof(EDIT_MENU) / sizeof(EDIT_MENU[0]);
        break;
    case 2:
        *entries = VIEW_MENU;
        *count   = sizeof(VIEW_MENU) / sizeof(VIEW_MENU[0]);
        break;
    case 3:
        *entries = IMAGE_MENU;
        *count   = sizeof(IMAGE_MENU) / sizeof(IMAGE_MENU[0]);
        break;
    default:
        *entries = HELP_MENU;
        *count   = 1;
        break;
    }
}

static Rect menu_header_rect(const App &app, int index)
{
    const char *zh[] = {"文件", "编辑", "查看", "图像", "帮助"};
    const char *en[] = {"File", "Edit", "View", "Image", "Help"};
    int x = 8;
    for (int i = 0; i <= index; ++i)
    {
        int width = maximum(60, (int)xapi_CalcTextWidth((char *)app.tr(zh[i], en[i]), 13) + 16);
        if (i == index) return {x, 2, width, 26};
        x += width + 4;
    }
    return {x, 2, 60, 26};
}

static void overlay_controls(App &app)
{
    if (app.modal != MODAL_NONE)
    {
        Rect box = dialog_rect(app);
        int  bw  = minimum(116, maximum(1, (box.w - 40) / 3));
        int  y   = box.y + box.h - 44;
        if (app.modal == MODAL_UNSAVED || app.modal == MODAL_OVERWRITE)
        {
            control(app, CMD_MODAL_SAVE, {box.x + 16, y, bw, 30},
                    app.modal == MODAL_OVERWRITE ? "替换 (S)" : "保存 (S)",
                    app.modal == MODAL_OVERWRITE ? "Replace (S)" : "Save (S)");
            if (app.modal == MODAL_UNSAVED)
                control(app, CMD_MODAL_DISCARD, {box.x + 22 + bw, y, bw, 30}, "不保存 (D)", "Discard (D)");
            control(app, CMD_MODAL_CANCEL, {box.x + box.w - bw - 16, y, bw, 30}, "取消", "Cancel");
        }
        else
        {
            if (app.modal != MODAL_ABOUT)
            {
                const int field_offset = modal_field_offset(app);
                control(app, CMD_FIELD_ONE, {box.x + field_offset, box.y + 80,
                        maximum(1, box.w - field_offset - 24), 32}, "", "");
                if (app.modal != MODAL_COLOR)
                    control(app, CMD_FIELD_TWO, {box.x + field_offset, box.y + 124,
                            maximum(1, box.w - field_offset - 24), 32}, "", "");
                control(app, CMD_MODAL_CANCEL, {box.x + box.w - 2 * bw - 22, y, bw, 30}, "取消", "Cancel");
            }
            control(app, CMD_MODAL_OK, {box.x + box.w - bw - 16, y, bw, 30}, "确定", "OK");
        }
    }
    else if (app.menu >= 0)
    {
        const MenuEntry *entries;
        int              count;
        menu_entries(app.menu, &entries, &count);
        int w = minimum(280, app.width - 8);
        int x = clamp(menu_header_rect(app, app.menu).x, 4, maximum(4, app.width - w - 4));
        for (int i = 0; i < count; ++i)
            control(app, entries[i].id, {x + 2, 32 + i * 30, w - 4, 30}, entries[i].zh, entries[i].en);
    }
}

void App::layout()
{
    control_count    = 0;
    const char *zh[] = {"文件", "编辑", "查看", "图像", "帮助"};
    const char *en[] = {"File", "Edit", "View", "Image", "Help"};
    for (int i = 0; i < 5; ++i)
        control(*this, CMD_MENU + i, menu_header_rect(*this, i), zh[i], en[i]);
    Flow quick = {*this, 8, 36, 30};
    quick.add(CMD_NEW, 76, "新建", "New", "file");
    quick.add(CMD_OPEN, 78, "打开", "Open", "folder-open");
    quick.add(CMD_SAVE, 78, "保存", "Save", "floppy-disk");
    quick.add(CMD_SAVE_AS, 100, "另存为", "Save As", "file-export");
    quick.add(CMD_UNDO, 80, "撤销", "Undo", "rotate-left");
    quick.add(CMD_REDO, 80, "重做", "Redo", "rotate-right");
    quick.add(CMD_CROP, 80, "裁剪", "Crop", "crop-simple");
    Flow        tools             = {*this, 8, quick.y + 38, 44};
    const char *icons[TOOL_COUNT] = {"pencil", "paintbrush",    "eraser", "fill-drip",    "eye-dropper",
                                     "minus",  "vector-square", "circle", "object-group", "hand"};
    for (int i = 0; i < TOOL_COUNT; ++i)
        tools.add(CMD_TOOL + i, 78, tool_name(*this, (Tool)i), tool_name(*this, (Tool)i), icons[i]);
    Flow options = {*this, 8, tools.y + 52, 28};
    options.add(CMD_SIZE_DOWN, 26, "-", "-");
    options.add(CMD_SIZE_UP, 68, "", "");
    options.add(CMD_SIZE_UP, 26, "+", "+");
    options.add(CMD_STYLE, 132, "", "");
    options.add(CMD_PRIMARY, 28, "", "");
    options.add(CMD_SECONDARY, 28, "", "");
    options.add(CMD_SWAP_COLORS, 28, "X", "X");
    options.add(CMD_CUSTOM_COLOR, 68, "颜色…", "Color...");
    for (int i = 0; i < 24; ++i)
        options.add(CMD_PALETTE + i, 18, "", "");
    int top   = minimum(options.y + 38, maximum(0, height - 72));
    view.area = {12, top, maximum(1, width - 36), maximum(1, height - top - 50)};
    if (view.area.x >= width) view.area.x = 0;
    if (view.area.y >= height) view.area.y = 0;
    view.area = intersect(view.area, {0, 0, width, maximum(1, height - 30)});
    // Keep a usable viewport when the OS allows a very short window. Menus
    // remain reachable; ribbon rows that no longer fit are hidden.
    int visible_count = 0;
    for (int i = 0; i < control_count; ++i)
        if (controls[i].rect.y + controls[i].rect.h <= view.area.y - 8 ||
            (controls[i].id >= CMD_MENU && controls[i].id < CMD_MENU + 5))
            controls[visible_count++] = controls[i];
    control_count = visible_count;
    view.constrain(canvas);
    horizontal_scroll = {view.area.x, view.area.y + view.area.h + 2, view.area.w, 10};
    vertical_scroll   = {view.area.x + view.area.w + 2, view.area.y, 10, view.area.h};
    int scaled_w = (canvas.width * view.zoom + 99) / 100, scaled_h = (canvas.height * view.zoom + 99) / 100;
    int thumb_w      = minimum(view.area.w, maximum(20, view.area.w * view.area.w / maximum(1, scaled_w)));
    int thumb_h      = minimum(view.area.h, maximum(20, view.area.h * view.area.h / maximum(1, scaled_h)));
    horizontal_thumb = {horizontal_scroll.x +
                            view.scroll_x * (view.area.w - thumb_w) / maximum(1, scaled_w - view.area.w),
                        horizontal_scroll.y, thumb_w, 10};
    vertical_thumb   = {vertical_scroll.x,
                        vertical_scroll.y + view.scroll_y * (view.area.h - thumb_h) / maximum(1, scaled_h - view.area.h),
                        10, thumb_h};
    int y            = maximum(0, height - 27);
    control(*this, CMD_ZOOM_OUT, {maximum(0, width - 218), y, 26, 24}, "-", "-");
    control(*this, CMD_ACTUAL_SIZE, {maximum(0, width - 188), y, 64, 24}, "", "");
    control(*this, CMD_ZOOM_IN, {maximum(0, width - 120), y, 26, 24}, "+", "+");
    control(*this, CMD_FIT, {maximum(0, width - 90), y, 82, 24}, "适应窗口", "Fit");
    overlay_controls(*this);
}

// Draw small app-owned glyphs with the same RGBA rasterizer as the canvas.
// This avoids depending on the system's small external icon/SVG resource set.
static void draw_icon(App &app, int id, int x, int y, UINT32 background)
{
    Canvas icon;
    if (!icon.create(20, 20, rgba(background >> 24, background >> 16, background >> 8))) return;
    Pixel blue = rgba(23, 107, 214), pale = rgba(219, 236, 255), gold = rgba(231, 158, 44);
    switch (id)
    {
    case CMD_NEW:
        draw_shape(icon, RECTANGLE, {4, 2}, {15, 17}, blue, pale, 1, OUTLINE_FILL);
        draw_stroke(icon, {7, 7}, {12, 7}, blue, 1);
        draw_stroke(icon, {7, 10}, {12, 10}, blue, 1);
        break;
    case CMD_OPEN:
        icon.fill({2, 5, 7, 4}, gold);
        draw_shape(icon, RECTANGLE, {2, 7}, {17, 16}, gold, rgba(255, 217, 143), 1, OUTLINE_FILL);
        draw_stroke(icon, {3, 10}, {16, 10}, gold, 1);
        break;
    case CMD_SAVE:
    case CMD_SAVE_AS:
        draw_shape(icon, RECTANGLE, {3, 2}, {17, 17}, blue, pale, 1, OUTLINE_FILL);
        icon.fill({6, 2, 8, 6}, blue);
        icon.fill({11, 3, 2, 4}, rgba(255, 255, 255));
        draw_shape(icon, RECTANGLE, {6, 11}, {14, 17}, blue, rgba(255, 255, 255), 1, OUTLINE_FILL);
        break;
    case CMD_UNDO:
    case CMD_REDO: {
        bool redo = id == CMD_REDO;
        int  left = redo ? 16 : 3, right = redo ? 4 : 15;
        draw_stroke(icon, {left, 7}, {right, 7}, blue, 2);
        draw_stroke(icon, {right, 7}, {right, 15}, blue, 2);
        draw_stroke(icon, {right, 15}, {8, 15}, blue, 2);
        draw_stroke(icon, {left, 7}, {left + (redo ? -4 : 4), 3}, blue, 2);
        draw_stroke(icon, {left, 7}, {left + (redo ? -4 : 4), 11}, blue, 2);
        break;
    }
    case CMD_CROP:
        draw_stroke(icon, {5, 2}, {5, 14}, blue, 2);
        draw_stroke(icon, {5, 14}, {18, 14}, blue, 2);
        draw_stroke(icon, {2, 5}, {14, 5}, blue, 2);
        draw_stroke(icon, {14, 5}, {14, 18}, blue, 2);
        break;
    case CMD_TOOL + PENCIL:
        draw_stroke(icon, {5, 14}, {15, 4}, gold, 4);
        draw_stroke(icon, {3, 17}, {5, 14}, blue, 2);
        draw_stroke(icon, {13, 3}, {16, 6}, blue, 2);
        break;
    case CMD_TOOL + BRUSH:
        draw_stroke(icon, {9, 11}, {16, 3}, gold, 3);
        draw_stroke(icon, {5, 16}, {9, 12}, blue, 6);
        draw_stroke(icon, {3, 18}, {7, 16}, blue, 2);
        break;
    case CMD_TOOL + ERASER:
        draw_stroke(icon, {5, 13}, {13, 5}, rgba(234, 126, 160), 8);
        draw_stroke(icon, {4, 14}, {7, 17}, blue, 3);
        draw_stroke(icon, {8, 18}, {17, 18}, blue, 1);
        break;
    case CMD_TOOL + FILL:
        draw_stroke(icon, {3, 8}, {8, 3}, blue, 2);
        draw_stroke(icon, {8, 3}, {14, 9}, blue, 2);
        draw_stroke(icon, {14, 9}, {9, 14}, blue, 2);
        draw_stroke(icon, {9, 14}, {3, 8}, blue, 2);
        draw_stroke(icon, {4, 8}, {12, 8}, gold, 2);
        draw_stroke(icon, {16, 13}, {16, 17}, blue, 3);
        break;
    case CMD_TOOL + PICKER:
        draw_stroke(icon, {4, 15}, {13, 6}, blue, 3);
        draw_stroke(icon, {11, 4}, {16, 9}, gold, 3);
        draw_stroke(icon, {14, 4}, {16, 2}, gold, 3);
        icon.set(2, 18, blue);
        break;
    case CMD_TOOL + LINE: draw_stroke(icon, {3, 17}, {17, 3}, blue, 2); break;
    case CMD_TOOL + RECTANGLE: draw_shape(icon, RECTANGLE, {2, 4}, {17, 15}, blue, pale, 2, OUTLINE_FILL); break;
    case CMD_TOOL + ELLIPSE: draw_shape(icon, ELLIPSE, {2, 4}, {17, 15}, blue, pale, 2, OUTLINE_FILL); break;
    case CMD_TOOL + SELECT:
        for (int i = 2; i < 18; i += 4)
        {
            icon.fill({i, 2, 2, 1}, blue);
            icon.fill({i, 17, 2, 1}, blue);
            icon.fill({2, i, 1, 2}, blue);
            icon.fill({17, i, 1, 2}, blue);
        }
        break;
    case CMD_TOOL + PAN:
        icon.fill({5, 9, 10, 6}, pale);
        for (int i = 5; i <= 13; i += 2)
            draw_stroke(icon, {i, 11}, {i, i == 5 ? 5 : 3}, blue, 1);
        draw_stroke(icon, {3, 10}, {6, 16}, blue, 2);
        draw_stroke(icon, {6, 16}, {13, 17}, blue, 2);
        draw_stroke(icon, {13, 17}, {16, 10}, blue, 2);
        break;
    default: break;
    }
    xapi_WriteBufferA(app.window, x, y, 20, 20, (XCOLORA *)icon.pixels);
}

static void draw_control(App &app, const Control &item, bool menu_item = false)
{
    if (item.id >= CMD_PALETTE && item.id < CMD_PALETTE + 24)
    {
        Pixel color = PALETTE[item.id - CMD_PALETTE];
        fill(app, {item.rect.x, item.rect.y + 4, item.rect.w, item.rect.h - 8}, ui_color(color));
        outline(app, {item.rect.x, item.rect.y + 4, item.rect.w, item.rect.h - 8}, BORDER);
        if (color == app.primary)
            outline(app, {item.rect.x - 1, item.rect.y + 3, item.rect.w + 2, item.rect.h - 6}, ACCENT);
        return;
    }
    if (item.id == CMD_PRIMARY || item.id == CMD_SECONDARY)
    {
        fill(app, item.rect, PANEL);
        Pixel color = item.id == CMD_PRIMARY ? app.primary : app.secondary;
        fill(app, {item.rect.x + 4, item.rect.y + 4, item.rect.w - 8, item.rect.h - 8}, ui_color(color));
        outline(app, item.rect, item.id == CMD_PRIMARY ? ACCENT : BORDER);
        return;
    }
    if (item.id == CMD_FIELD_ONE || item.id == CMD_FIELD_TWO)
    {
        int field = item.id == CMD_FIELD_ONE ? 0 : 1;
        fill(app, item.rect, PANEL);
        outline(app, item.rect, field == app.field ? ACCENT : BORDER);
        if (field == app.field && app.replace_input)
            fill(app, {item.rect.x + 5, item.rect.y + 5, minimum(item.rect.w - 10, 90), item.rect.h - 10}, SELECTED);
        text(app, item.rect.x + 10, xtext_reference_y(item.rect.y, item.rect.h, XTEXT_FONT_UI, 14),
             app.input[field], TEXT, 14, item.rect.w - 20);
        return;
    }
    bool menu_header = item.id >= CMD_MENU && item.id < CMD_MENU + 5;
    bool active      = item.selected || (menu_header && app.menu == item.id - CMD_MENU);
    bool hovered     = item.enabled && item.rect.contains(app.mouse.x, app.mouse.y);
    fill(app, item.rect, active ? SELECTED : hovered ? 0xeff5fcff : PANEL);
    if (!menu_header && !menu_item) outline(app, item.rect, active ? ACCENT : BORDER);
    const char *label = app.tr(item.zh, item.en);
    char        dynamic[64];
    if (item.id == CMD_SIZE_UP && !item.zh[0])
    {
        snprintf(dynamic, sizeof(dynamic), "%d px", app.brush_size);
        label = dynamic;
    }
    if (item.id == CMD_ACTUAL_SIZE)
    {
        snprintf(dynamic, sizeof(dynamic), "%d%%", app.view.zoom);
        label = dynamic;
    }
    if (item.id == CMD_STYLE)
    {
        const char *zh[] = {"轮廓", "填充", "轮廓 + 填充"};
        const char *en[] = {"Outline", "Fill", "Outline + Fill"};
        label            = app.tr(zh[app.style], en[app.style]);
    }
    UINT32 color       = !item.enabled ? 0xa6b0bfff : active ? ACCENT : TEXT;
    bool   tool_button = item.id >= CMD_TOOL && item.id < CMD_TOOL + TOOL_COUNT;
    int    tx = item.rect.x + 8, ty = xtext_reference_y(item.rect.y, item.rect.h, XTEXT_FONT_UI, 13);
    if (item.icon && item.enabled)
    {
        int ix = tool_button ? item.rect.x + item.rect.w / 2 - 10 : item.rect.x + 6;
        int iy = tool_button ? item.rect.y + 3 : item.rect.y + 4;
        if (ix >= 0 && iy >= 0 && ix + 20 <= app.width && iy + 20 <= app.height)
            draw_icon(app, item.id, ix, iy, active ? SELECTED : hovered ? 0xeff5fcff : PANEL);
        if (!tool_button) tx += 22;
    }
    if (tool_button)
    {
        tx = item.rect.x + maximum(3, (item.rect.w - (int)xapi_CalcTextWidth((char *)label, 12)) / 2);
        ty = xtext_reference_y(item.rect.y + 25, item.rect.h - 25, XTEXT_FONT_UI, 12);
    }
    if (item.id >= CMD_MODAL_SAVE && item.id <= CMD_MODAL_OK)
        xapi_DrawTextInRect(app.window, item.rect.x, item.rect.y, item.rect.w, item.rect.h, (char *)label, 13, color);
    else
        text(app, tx, ty, label, color, tool_button ? 12 : 13, item.rect.x + item.rect.w - tx - 5);
}

void draw_status(App &app)
{
    fill(app, {0, app.height - 30, app.width, 30}, PANEL);
    Point point = app.view.image_point(app.canvas, app.mouse.x, app.mouse.y);
    char  label[288], coordinates[32];
    if (app.view.area.contains(app.mouse.x, app.mouse.y) && app.canvas.bounds().contains(point.x, point.y))
        snprintf(coordinates, sizeof(coordinates), "%d, %d", point.x, point.y);
    else
        snprintf(coordinates, sizeof(coordinates), "--");
    if (app.width >= 850)
        snprintf(label, sizeof(label), "%s  |  %d x %d px  |  %s  |  %s", tool_name(app, app.tool), app.canvas.width,
                 app.canvas.height, coordinates, app.status);
    else
        snprintf(label, sizeof(label), "%s | %d x %d px", tool_name(app, app.tool), app.canvas.width,
                 app.canvas.height);
    text(app, 12, xtext_reference_y(app.height - 30, 30, XTEXT_FONT_UI, 12),
         label, MUTED, 12, maximum(0, app.width - 244));
    for (int i = 0; i < app.control_count; ++i)
        if (app.controls[i].id >= CMD_ZOOM_OUT && app.controls[i].id <= CMD_FIT) draw_control(app, app.controls[i]);
}

void draw_ui(App &app)
{
    fill(app, {0, 0, app.width, app.height}, BG);
    fill(app, {0, 0, app.width, maximum(0, app.view.area.y - 8)}, PANEL);
    fill(app, {0, 29, app.width, 1}, BORDER);
    if (app.width > 460) text(app, app.width - 126, 7, "xPhoton", ACCENT, 16, 120);
    for (int i = 0; i < app.control_count; ++i)
    {
        const Control &item = app.controls[i];
        if (item.rect.y >= app.height - 30) continue;
        // Dropdown rows and modal controls are drawn in the overlay pass.
        if ((app.menu >= 0 && item.id < CMD_TOOL && item.rect.y >= 32 && item.rect.h == 30 && item.rect.w > 160) ||
            item.id >= CMD_MODAL_SAVE)
            continue;
        draw_control(app, item);
    }
    fill(app, app.horizontal_scroll, 0xe1e7efff);
    fill(app, app.vertical_scroll, 0xe1e7efff);
    fill(app, app.horizontal_thumb, 0xa4b4c8ff);
    fill(app, app.vertical_thumb, 0xa4b4c8ff);
    draw_status(app);
}

static Pixel composite(Pixel foreground, Pixel background)
{
    unsigned alpha = foreground >> 24;
    unsigned inv   = 255 - alpha;
    return rgba(((foreground & 255) * alpha + (background & 255) * inv + 127) / 255,
                (((foreground >> 8) & 255) * alpha + ((background >> 8) & 255) * inv + 127) / 255,
                (((foreground >> 16) & 255) * alpha + ((background >> 16) & 255) * inv + 127) / 255);
}

void draw_canvas(App &app, Rect region)
{
    region = intersect(region, app.view.area);
    if (region.empty()) return;
    // Upload tightly packed strips. This bounds transient rendering memory and
    // avoids a syscall per painted pixel or a full-window buffer for each drag.
    const int rows  = 24;
    size_t    count = (size_t)region.w * minimum(rows, region.h);
    Pixel    *strip = (Pixel *)malloc(count * sizeof(Pixel));
    if (!strip)
    {
        app.set_status("显示缓冲区内存不足", "Not enough memory for display buffer");
        return;
    }
    Point origin   = app.view.origin(app.canvas);
    Rect  image    = app.view.screen_rect(app.canvas, app.canvas.bounds());
    Rect  selected = app.view.screen_rect(app.canvas, app.selection);
    for (int top = region.y; top < region.y + region.h; top += rows)
    {
        int height = minimum(rows, region.y + region.h - top);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < region.w; ++x)
            {
                int   sx = region.x + x, sy = top + y;
                Pixel color = rgba(232, 237, 244);
                if (image.contains(sx, sy))
                {
                    int ix = (sx - origin.x) * 100 / app.view.zoom;
                    int iy = (sy - origin.y) * 100 / app.view.zoom;
                    if (app.canvas.bounds().contains(ix, iy))
                    {
                        Pixel checker = ((sx / 8 + sy / 8) & 1) ? rgba(229, 233, 240) : rgba(255, 255, 255);
                        color         = composite(app.canvas.get(ix, iy), checker);
                    }
                }
                else if (sx >= image.x + 4 && sy >= image.y + 4 && sx < image.x + image.w + 4 &&
                         sy < image.y + image.h + 4)
                    color = rgba(194, 204, 218);
                if (sx >= image.x - 1 && sx <= image.x + image.w && sy >= image.y - 1 && sy <= image.y + image.h &&
                    (sx == image.x - 1 || sx == image.x + image.w || sy == image.y - 1 || sy == image.y + image.h))
                    color = rgba(166, 181, 201);
                if (!app.selection.empty() && selected.contains(sx, sy) &&
                    (sx == selected.x || sy == selected.y || sx == selected.x + selected.w - 1 ||
                     sy == selected.y + selected.h - 1))
                    color = ((sx + sy) / 4) & 1 ? rgba(255, 255, 255) : rgba(23, 107, 214);
                strip[(size_t)y * region.w + x] = color;
            }
        xapi_WriteBufferA(app.window, region.x, top, region.w, height, (XCOLORA *)strip);
    }
    free(strip);
}

void draw_overlays(App &app)
{
    if (app.modal != MODAL_NONE)
    {
        Rect box = dialog_rect(app);
        fill(app, {box.x + 5, box.y + 5, box.w, box.h}, 0x64748bff);
        fill(app, box, BG);
        outline(app, box, BORDER);
        fill(app, {box.x, box.y, box.w, 3}, ACCENT);
        const char *title = "xPhoton";
        switch (app.modal)
        {
        case MODAL_UNSAVED: title = app.tr("保存修改？", "Save changes?"); break;
        case MODAL_OVERWRITE: title = app.tr("替换已有图片？", "Replace existing image?"); break;
        case MODAL_NEW: title = app.tr("新建画布", "New canvas"); break;
        case MODAL_CANVAS_SIZE: title = app.tr("调整画布尺寸", "Canvas size"); break;
        case MODAL_IMAGE_SIZE: title = app.tr("缩放图像", "Resize image"); break;
        case MODAL_COLOR: title = app.tr("自定义颜色", "Custom color"); break;
        case MODAL_ABOUT: title = "xPhoton"; break;
        default: break;
        }
        text(app, box.x + 20, box.y + 22, title, ACCENT, 18, box.w - 40);
        if (app.modal == MODAL_UNSAVED)
        {
            text(app, box.x + 20, box.y + 70, app.tr("当前图片有尚未保存的修改。", "This image has unsaved changes."),
                 TEXT, 14, box.w - 40);
            text(app, box.x + 20, box.y + 104,
                 app.tr("请选择保存、不保存或取消。", "Save, discard, or cancel to keep editing."), MUTED, 13,
                 box.w - 40);
        }
        else if (app.modal == MODAL_OVERWRITE)
        {
            text(app, box.x + 20, box.y + 70, app.pending_path, TEXT, 13, box.w - 40);
            text(app, box.x + 20, box.y + 104,
                 app.tr("此文件已存在。替换后将保存当前图片。", "This file exists. Replace it with the current image?"),
                 MUTED, 13, box.w - 40);
        }
        else if (app.modal == MODAL_ABOUT)
        {
            const char *lines_zh[] = {"为 XJ380 设计的轻量位图编辑器",
                                      "PNG / BMP 保存 · PNG / BMP / JPEG 打开",
                                      "B 画笔 · P 铅笔 · E 橡皮 · F 填充 · I 吸管",
                                      "L 直线 · R 矩形 · O 椭圆 · S 选区 · H 平移",
                                      "Ctrl+滚轮缩放 · 空格 / 中键拖动画布",
                                      "Shift 约束形状 · [ ] 调整粗细 · X 交换颜色",
                                      "Ctrl+Z / Y 撤销重做 · Ctrl+S 保存",
                                      "未保存时请通过“文件 → 退出”关闭。"};
            const char *lines_en[] = {
                "A lightweight bitmap editor for XJ380",         "Save PNG / BMP; open PNG / BMP / JPEG",
                "B Brush  P Pencil  E Eraser  F Fill  I Picker", "L Line  R Rectangle  O Ellipse  S Select  H Pan",
                "Ctrl+wheel: zoom; Space / middle drag: pan",    "Shift: constrain; [ ]: size; X: swap colors",
                "Ctrl+Z / Y: undo / redo; Ctrl+S: save",         "Use File > Exit to save changes before closing."};
            for (int i = 0; i < 8; ++i)
                text(app, box.x + 20, box.y + 60 + i * 25, app.tr(lines_zh[i], lines_en[i]), i ? MUTED : TEXT, 12,
                     box.w - 40);
        }
        else
        {
            const int label_width = maximum(0, modal_field_offset(app) - 32);
            text(app, box.x + 20, xtext_reference_y(box.y + 80, 32, XTEXT_FONT_UI, 13),
                 app.modal == MODAL_COLOR ? "Hex RGB #" : app.tr("宽度（像素）", "Width (px)"), TEXT, 13, label_width);
            if (app.modal != MODAL_COLOR)
                text(app, box.x + 20, xtext_reference_y(box.y + 124, 32, XTEXT_FONT_UI, 13),
                     app.tr("高度（像素）", "Height (px)"), TEXT, 13, label_width);
            const char *hint =
                app.modal == MODAL_COLOR ? app.tr("输入六位颜色，例如 176BD6", "Six digits, for example 176BD6")
                : app.modal == MODAL_IMAGE_SIZE
                    ? app.tr("最近邻缩放；Tab 切换输入框", "Nearest neighbor; Tab switches fields")
                    : app.tr("左上角固定；总像素不超过 8,388,608", "Top-left anchored; at most 8,388,608 pixels");
            text(app, box.x + 20, box.y + box.h - 78, hint, MUTED, 12, box.w - 40);
        }
        for (int i = 0; i < app.control_count; ++i)
            if (app.controls[i].id >= CMD_MODAL_SAVE) draw_control(app, app.controls[i]);
    }
    else if (app.menu >= 0)
    {
        const MenuEntry *entries;
        int              count;
        menu_entries(app.menu, &entries, &count);
        int w = minimum(280, app.width - 8);
        int x = clamp(menu_header_rect(app, app.menu).x, 4, maximum(4, app.width - w - 4));
        fill(app, {x + 4, 34, w, count * 30 + 4}, 0x9facbeff);
        fill(app, {x, 30, w, count * 30 + 4}, PANEL);
        outline(app, {x, 30, w, count * 30 + 4}, BORDER);
        for (int i = app.control_count - count; i < app.control_count; ++i)
            draw_control(app, app.controls[i], true);
    }
}

int hit_control(const App &app, int x, int y)
{
    // Overlay controls win hit testing, including when they cover the ribbon.
    for (int i = app.control_count - 1; i >= 0; --i)
    {
        const Control &item = app.controls[i];
        if (app.modal != MODAL_NONE && item.id < CMD_MODAL_SAVE) continue;
        if (app.menu >= 0 && item.id < CMD_MENU)
        {
            const MenuEntry *entries;
            int              count;
            menu_entries(app.menu, &entries, &count);
            if (i < app.control_count - count) continue;
        }
        if (item.enabled && item.rect.contains(x, y)) return item.id;
    }
    return CMD_NONE;
}

} // namespace xphoton
