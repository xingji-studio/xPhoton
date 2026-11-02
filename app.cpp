#include "app.h"
#include "image_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xj380_i18n.h>
#include <xpermissions.h>

namespace xphoton {

App::App()
    : window(0), tool(BRUSH), style(OUTLINE), primary(rgba(24, 34, 50)), secondary(rgba(255, 255, 255)), brush_size(4),
      width(1024), height(720), language(0), running(true), ctrl(false), shift(false), space(false), repaint_ui(true),
      repaint_view(true), repaint_status(true), suppress_click(false), pressed_control(0), dirty_region{0, 0, 0, 0},
      selection{0, 0, 0, 0}, mouse{0, 0}, start{0, 0}, last{0, 0}, pan_start{0, 0}, drag(DRAG_NONE), drag_button(0),
      scroll_start_x(0), scroll_start_y(0), move_source{0, 0, 0, 0}, modal(MODAL_NONE), pending_command(0), menu(-1),
      field(0), replace_input(true), color_secondary(false), control_count(0)
{
    path[0] = pending_path[0] = status[0] = '\0';
    input[0][0] = input[1][0] = '\0';
    view                      = {
        {0, 0, 1, 1},
        100, 0, 0
    };
    horizontal_scroll = vertical_scroll = horizontal_thumb = vertical_thumb = {0, 0, 0, 0};
}

const char *App::tr(const char *zh, const char *en) const
{
    return xj380_tr_lang(language, zh, en);
}

bool App::initialize()
{
    language = xj380_read_language();
    if (!canvas.create(800, 600)) return false;
    set_status("就绪", "Ready");
    return true;
}

void App::set_status(const char *zh, const char *en)
{
    snprintf(status, sizeof(status), "%s", tr(zh, en));
    repaint_ui = true;
}

void App::update_title()
{
    if (!window) return;
    char title[576];
    snprintf(title, sizeof(title), "%s%s - xPhoton", path[0] ? path : tr("未命名", "Untitled"),
             history.dirty() ? " *" : "");
    xapi_SetWindowTitle(window, title);
}

void App::invalidate(Rect rect)
{
    dirty_region = unite(dirty_region, intersect(rect, view.area));
    repaint_view = true;
}

void App::invalidate_all()
{
    repaint_ui = repaint_view = true;
    dirty_region              = view.area;
}

bool App::begin_edit()
{
    if (history.begin(canvas)) return true;
    set_status("内存不足，操作未开始", "Not enough memory to start this operation");
    return false;
}

void App::commit_edit()
{
    if (!history.commit(canvas)) set_status("内存不足，已还原本次操作", "Not enough memory; operation was restored");
    view.constrain(canvas);
    update_title();
    repaint_ui = true;
}

void App::cancel_drag()
{
    if (history.active()) history.cancel(canvas);
    if (drag == DRAG_MOVE) selection = move_source;
    if (drag == DRAG_SELECT) selection = {0, 0, 0, 0};
    drag        = DRAG_NONE;
    drag_button = 0;
    floating.reset();
    invalidate_all();
}

void App::input_lost()
{
    cancel_drag();
    UINT64 actual_width = 0, actual_height = 0;
    xapi_GetWindowSize(window, &actual_width, &actual_height);
    if (actual_width && actual_height)
    {
        width  = (int)minimum((int)actual_width, 16384);
        height = (int)minimum((int)actual_height, 16384);
        layout();
    }
    ctrl = shift = space = false;
    set_status("输入过快，已取消当前拖动；请继续操作", "Input queue recovered; the current drag was cancelled");
}

static bool absolute_image_path(const char *filename, char *output, size_t capacity)
{
    if (!filename || !filename[0] || strlen(filename) > 480) return false;
    int length;
    if (filename[0] == '/')
        length = snprintf(output, capacity, "%s", filename);
    else
    {
        char directory[512];
        if (!getcwd(directory, sizeof(directory))) return false;
        length = snprintf(output, capacity, "%s%s%s", directory, strcmp(directory, "/") ? "/" : "", filename);
    }
    return length > 0 && length <= 480 && (size_t)length < capacity;
}

static void default_image_path(char *output, size_t capacity)
{
    UserInfo user = {};
    xapi_GetCurrentUser(&user);
    user.name[sizeof(user.name) - 1] = '\0';
    char directory[160];
    snprintf(directory, sizeof(directory), "/users/%s/desktop", user.name[0] ? user.name : "Root");
    struct stat st;
    if (stat(directory, &st) == 0 && S_ISDIR(st.st_mode))
        snprintf(output, capacity, "%s/xPhoton.png", directory);
    else if (stat("/users", &st) == 0 && S_ISDIR(st.st_mode))
        snprintf(output, capacity, "/users/xPhoton.png");
    else
        snprintf(output, capacity, "/xPhoton.png");
}

bool App::load(const char *filename)
{
    // Resolve relative launch arguments before passing a path to the system
    // file dialog, which requires an absolute directory/file path.
    char source_path[512];
    if (!absolute_image_path(filename, source_path, sizeof(source_path)))
    {
        set_status("图片路径为空或过长", "The image path is empty or too long");
        return false;
    }
    if (xj380_ensure_file_access(source_path, XFA_READ) < 0)
    {
        set_status("未获得读取权限，原画布已保留", "Read permission denied; the current canvas was kept");
        return false;
    }
    filename = source_path;
    UINT32 w = 0, h = 0;
    xapi_GetPicSize(&w, &h, (char *)filename);
    if (w > MAX_SIDE || h > MAX_SIDE || !Canvas::valid_size((int)w, (int)h))
    {
        set_status("无法打开图片，或图片超过 8192 边长 / 800 万像素限制",
                   "Cannot open image, or image exceeds size limits");
        return false;
    }
    Canvas image;
    if (!image.create((int)w, (int)h))
    {
        set_status("内存不足，无法打开图片", "Not enough memory to open image");
        return false;
    }
    if (!xapi_LoadPicture((XCOLORA *)image.pixels, w, h, (char *)filename))
    {
        set_status("图片解码失败，原画布已保留", "Image decoding failed; the current canvas was kept");
        return false;
    }
    cancel_drag();
    canvas.swap(image);
    history.reset();
    selection = {0, 0, 0, 0};
    snprintf(path, sizeof(path), "%s", filename);
    view.fit(canvas);
    update_title();
    set_status("图片已打开", "Image opened");
    invalidate_all();
    return true;
}

bool App::save(bool save_as)
{
    char filename[512];
    bool chose_path = save_as || !path[0] || !writable_format(path);
    snprintf(filename, sizeof(filename), "%s", path);
    if (save_as || !filename[0] || !writable_format(filename))
    {
        if (!writable_format(filename))
        {
            char *dot   = strrchr(filename, '.');
            char *slash = strrchr(filename, '/');
            if (dot && (!slash || dot > slash)) *dot = '\0';
            size_t n = strlen(filename);
            if (!n)
                default_image_path(filename, sizeof(filename));
            else if (n + 4 < sizeof(filename))
                strcat(filename, ".png");
        }
        char   chosen[512] = {};
        UINT64 result =
            xapi_FileDialog(XAPI_FILE_DIALOG_SAVE, (char *)tr("保存图片（PNG / BMP）", "Save image (PNG / BMP)"),
                            filename, chosen, sizeof(chosen));
        discard_input();
        ctrl = shift = space = false;
        if ((INT64)result < 0 || !chosen[0])
        {
            invalidate_all();
            return false;
        }
        snprintf(filename, sizeof(filename), "%s", chosen);
        if (!strrchr(strrchr(filename, '/') ? strrchr(filename, '/') + 1 : filename, '.'))
        {
            if (strlen(filename) + 4 >= sizeof(filename))
            {
                set_status("路径过长", "Path is too long");
                return false;
            }
            strcat(filename, ".png");
        }
    }
    if (xj380_ensure_file_access(filename, XFA_WRITE | XFA_CREATE) < 0)
    {
        set_status("未获得保存权限，画布修改仍保留", "Save permission denied; canvas changes were kept");
        return false;
    }
    struct stat st;
    if (chose_path && writable_format(filename) && stat(filename, &st) == 0)
    {
        snprintf(pending_path, sizeof(pending_path), "%s", filename);
        open_modal(MODAL_OVERWRITE);
        return false;
    }
    return write_to(filename);
}

bool App::write_to(const char *filename)
{
    if (xj380_ensure_file_access(filename, XFA_WRITE | XFA_CREATE) < 0)
    {
        set_status("未获得保存权限，画布修改仍保留", "Save permission denied; canvas changes were kept");
        return false;
    }
    char     recovery[576];
    IoResult result = save_image(canvas, filename, recovery, sizeof(recovery));
    if (result != IO_OK)
    {
        if (result == IO_FORMAT)
            set_status("请使用 .png 或 .bmp 扩展名保存", "Please save with a .png or .bmp extension");
        else if (result == IO_RECOVERY)
        {
            snprintf(status, sizeof(status), "%s: %.140s", tr("原文件保留于", "Original file retained at"), recovery);
            repaint_ui = true;
        }
        else
            set_status("保存失败，画布修改仍保留", "Save failed; canvas changes are still available");
        invalidate_all();
        return false;
    }
    snprintf(path, sizeof(path), "%s", filename);
    history.mark_saved();
    update_title();
    set_status("图片已保存", "Image saved");
    invalidate_all();
    return true;
}

void App::open_modal(Modal kind)
{
    modal         = kind;
    menu          = -1;
    field         = 0;
    replace_input = true;
    if (kind == MODAL_NEW || kind == MODAL_CANVAS_SIZE || kind == MODAL_IMAGE_SIZE)
    {
        snprintf(input[0], sizeof(input[0]), "%d", kind == MODAL_NEW ? 800 : canvas.width);
        snprintf(input[1], sizeof(input[1]), "%d", kind == MODAL_NEW ? 600 : canvas.height);
    }
    if (kind == MODAL_COLOR)
    {
        Pixel color = color_secondary ? secondary : primary;
        snprintf(input[0], sizeof(input[0]), "%02X%02X%02X", (unsigned)(color & 255), (unsigned)((color >> 8) & 255),
                 (unsigned)((color >> 16) & 255));
    }
    invalidate_all();
}

void App::request_document_command(int id)
{
    cancel_drag();
    pending_command = id;
    if (history.dirty())
        open_modal(MODAL_UNSAVED);
    else
        execute_document_command(id);
}

void App::execute_document_command(int id)
{
    pending_command = 0;
    modal           = MODAL_NONE;
    if (id == CMD_NEW)
        open_modal(MODAL_NEW);
    else if (id == CMD_OPEN)
    {
        char   chosen[512] = {};
        UINT64 result =
            xapi_FileDialog(XAPI_FILE_DIALOG_OPEN, (char *)tr("打开图片", "Open image"), path, chosen, sizeof(chosen));
        discard_input();
        ctrl = shift = space = false;
        if ((INT64)result >= 0 && chosen[0]) load(chosen);
        invalidate_all();
    }
    else if (id == CMD_EXIT) { running = false; }
}

void App::accept_modal()
{
    if (modal == MODAL_ABOUT)
    {
        modal = MODAL_NONE;
        invalidate_all();
        return;
    }
    if (modal == MODAL_COLOR)
    {
        if (strlen(input[0]) != 6)
        {
            set_status("请输入六位十六进制颜色", "Enter six hexadecimal digits");
            return;
        }
        unsigned value = 0;
        for (int i = 0; i < 6; ++i)
        {
            char c     = input[0][i];
            int  digit = c >= '0' && c <= '9'   ? c - '0'
                         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                : -1;
            if (digit < 0) return;
            value = (value << 4) | (unsigned)digit;
        }
        Pixel color = rgba(value >> 16, value >> 8, value);
        if (color_secondary)
            secondary = color;
        else
            primary = color;
    }
    else
    {
        int w = atoi(input[0]), h = atoi(input[1]);
        if (!Canvas::valid_size(w, h))
        {
            set_status("尺寸须为 1–8192，且总像素不超过 8,388,608", "Use 1-8192 per side, at most 8,388,608 pixels");
            return;
        }
        if (modal == MODAL_NEW)
        {
            Canvas image;
            if (!image.create(w, h, secondary))
            {
                set_status("内存不足", "Not enough memory");
                return;
            }
            image.swap(canvas);
            history.reset();
            path[0] = '\0';
        }
        else
        {
            if (!begin_edit()) return;
            if (!resize_image(canvas, w, h, modal == MODAL_IMAGE_SIZE, secondary))
            {
                history.cancel(canvas);
                set_status("内存不足", "Not enough memory");
                return;
            }
            commit_edit();
        }
        selection = {0, 0, 0, 0};
        view.fit(canvas);
        update_title();
    }
    modal = MODAL_NONE;
    set_status("就绪", "Ready");
    invalidate_all();
}

void App::copy_selection(bool cut)
{
    if (selection.empty()) return;
    if (!copy_region(canvas, selection, clipboard))
    {
        set_status("内存不足", "Not enough memory");
        return;
    }
    size_t      bytes = sizeof(XClipImage) + (size_t)clipboard.width * clipboard.height * sizeof(Pixel);
    XClipImage *image = (XClipImage *)malloc(bytes);
    if (!image)
    {
        set_status("内存不足", "Not enough memory");
        return;
    }
    *image = {sizeof(*image),
              XCLIP_ABI_VERSION,
              (XClipU32)clipboard.width,
              (XClipU32)clipboard.height,
              (XClipU32)clipboard.width * 4,
              0};
    memcpy(image + 1, clipboard.pixels, bytes - sizeof(*image));
    XClipOffer offer   = {};
    offer.size         = sizeof(offer);
    offer.version      = XCLIP_ABI_VERSION;
    offer.count        = 1;
    offer.entries[0]   = {XCLIP_RGBA8, 0, (XClipU64)image, bytes};
    XClipResult result = xapi_ClipboardSetData(&offer);
    free(image);
    if (result < 0)
    {
        set_status("复制失败，原图未更改", "Copy failed; image was preserved");
        return;
    }
    set_status("已复制到系统剪贴板", "Copied to the system clipboard");
    if (cut) delete_selection();
}

void App::delete_selection()
{
    if (selection.empty() || !begin_edit()) return;
    canvas.fill(selection, secondary);
    commit_edit();
    invalidate_all();
}

void App::paste()
{
    if (xapi_RequestPaste(window, 0) < 0) set_status("剪贴板中没有可粘贴的图像", "The clipboard has no image to paste");
}

void App::receive_paste(UINT64 paste_id)
{
    XClipInfo info;
    if (modal != MODAL_NONE || drag != DRAG_NONE || xapi_GetPasteInfo(paste_id, &info) < 0 || info.window != window)
    {
        xapi_FinishPaste(paste_id, 0);
        return;
    }
    XClipU64 bytes = 0;
    for (unsigned i = 0; i < info.count; ++i)
        if (info.entries[i].format == XCLIP_RGBA8) bytes = info.entries[i].bytes;
    if (bytes < sizeof(XClipImage) || bytes > sizeof(XClipImage) + (XClipU64)MAX_PIXELS * 4)
    {
        xapi_FinishPaste(paste_id, 0);
        return;
    }
    XClipImage *image = (XClipImage *)malloc((size_t)bytes);
    Canvas      imported;
    bool        loaded = image && xapi_ReadPaste(paste_id, XCLIP_RGBA8, image, bytes) == (XClipResult)bytes &&
                  image->size == sizeof(*image) && image->version == XCLIP_ABI_VERSION &&
                  Canvas::valid_size(image->width, image->height) && image->stride == image->width * 4 &&
                  bytes == sizeof(*image) + (XClipU64)image->stride * image->height &&
                  imported.create(image->width, image->height);
    if (loaded) memcpy(imported.pixels, image + 1, (size_t)bytes - sizeof(*image));
    free(image);
    xapi_FinishPaste(paste_id, 0);
    if (!loaded || !begin_edit()) return;
    imported.swap(clipboard);
    Point p = view.image_point(canvas, view.area.x + 16, view.area.y + 16);
    p.x     = clamp(p.x, 0, maximum(0, canvas.width - clipboard.width));
    p.y     = clamp(p.y, 0, maximum(0, canvas.height - clipboard.height));
    paste_region(canvas, clipboard, p.x, p.y);
    selection = intersect({p.x, p.y, clipboard.width, clipboard.height}, canvas.bounds());
    tool      = SELECT;
    commit_edit();
    invalidate_all();
}

void App::transform(Transform kind)
{
    if (!begin_edit()) return;
    if (!transform_image(canvas, kind))
    {
        history.cancel(canvas);
        set_status("内存不足", "Not enough memory");
        return;
    }
    selection = {0, 0, 0, 0};
    commit_edit();
    view.fit(canvas);
    invalidate_all();
}

void App::zoom(int value)
{
    view.zoom_at(canvas, value, mouse.x, mouse.y);
    invalidate_all();
}

void App::command(int id, bool right)
{
    if (id == CMD_NONE) return;
    if (id >= CMD_MODAL_SAVE && id <= CMD_FIELD_TWO)
    {
        if (id == CMD_MODAL_CANCEL)
        {
            modal           = MODAL_NONE;
            pending_command = 0;
        }
        else if (id == CMD_MODAL_DISCARD)
        {
            int next = pending_command;
            execute_document_command(next);
        }
        else if (id == CMD_MODAL_SAVE)
        {
            int  next  = pending_command;
            bool saved = modal == MODAL_OVERWRITE ? write_to(pending_path) : save(false);
            if (saved)
            {
                modal           = MODAL_NONE;
                pending_command = 0;
                if (next) execute_document_command(next);
            }
        }
        else if (id == CMD_MODAL_OK)
            accept_modal();
        else
        {
            field         = id == CMD_FIELD_TWO ? 1 : 0;
            replace_input = true;
        }
        invalidate_all();
        return;
    }
    if (modal != MODAL_NONE) return;
    if (id >= CMD_MENU && id < CMD_MENU + 5)
    {
        menu = menu == id - CMD_MENU ? -1 : id - CMD_MENU;
        invalidate_all();
        return;
    }
    menu = -1;
    if (drag != DRAG_NONE) cancel_drag();
    if (id >= CMD_TOOL && id < CMD_TOOL + TOOL_COUNT)
    {
        tool = (Tool)(id - CMD_TOOL);
        if (tool != SELECT) selection = {0, 0, 0, 0};
        set_status("就绪", "Ready");
        invalidate_all();
        return;
    }
    if (id >= CMD_PALETTE && id < CMD_PALETTE + 24)
    {
        extern const Pixel PALETTE[24];
        if (right)
            secondary = PALETTE[id - CMD_PALETTE];
        else
            primary = PALETTE[id - CMD_PALETTE];
        repaint_ui = true;
        return;
    }
    switch (id)
    {
    case CMD_NEW:
    case CMD_OPEN:
    case CMD_EXIT: request_document_command(id); break;
    case CMD_SAVE: save(false); break;
    case CMD_SAVE_AS: save(true); break;
    case CMD_UNDO:
        history.undo(canvas);
        selection = {0, 0, 0, 0};
        view.constrain(canvas);
        update_title();
        break;
    case CMD_REDO:
        history.redo(canvas);
        selection = {0, 0, 0, 0};
        view.constrain(canvas);
        update_title();
        break;
    case CMD_CUT: copy_selection(true); break;
    case CMD_COPY: copy_selection(false); break;
    case CMD_PASTE: paste(); break;
    case CMD_DELETE: delete_selection(); break;
    case CMD_SELECT_ALL:
        tool      = SELECT;
        selection = canvas.bounds();
        break;
    case CMD_DESELECT: selection = {0, 0, 0, 0}; break;
    case CMD_CROP:
        if (!selection.empty() && begin_edit())
        {
            Canvas cropped;
            if (copy_region(canvas, selection, cropped))
            {
                canvas.swap(cropped);
                commit_edit();
                view.fit(canvas);
            }
            else
            {
                history.cancel(canvas);
                set_status("内存不足", "Not enough memory");
            }
            selection = {0, 0, 0, 0};
        }
        break;
    case CMD_RESIZE_CANVAS: open_modal(MODAL_CANVAS_SIZE); break;
    case CMD_RESIZE_IMAGE: open_modal(MODAL_IMAGE_SIZE); break;
    case CMD_ROTATE_RIGHT: transform(ROTATE_RIGHT); break;
    case CMD_ROTATE_LEFT: transform(ROTATE_LEFT); break;
    case CMD_FLIP_H: transform(FLIP_HORIZONTAL); break;
    case CMD_FLIP_V: transform(FLIP_VERTICAL); break;
    case CMD_ZOOM_OUT:
        mouse = {view.area.x + view.area.w / 2, view.area.y + view.area.h / 2};
        zoom(view.zoom > 100 ? view.zoom - 100 : view.zoom - 25);
        break;
    case CMD_ZOOM_IN:
        mouse = {view.area.x + view.area.w / 2, view.area.y + view.area.h / 2};
        zoom(view.zoom >= 100 ? view.zoom + 100 : view.zoom + 25);
        break;
    case CMD_ACTUAL_SIZE:
        mouse = {view.area.x + view.area.w / 2, view.area.y + view.area.h / 2};
        zoom(100);
        break;
    case CMD_FIT: view.fit(canvas); break;
    case CMD_SIZE_DOWN: brush_size = maximum(1, brush_size - (brush_size > 8 ? 4 : 1)); break;
    case CMD_SIZE_UP: brush_size = minimum(64, brush_size + (brush_size >= 8 ? 4 : 1)); break;
    case CMD_STYLE: style = (ShapeStyle)((style + 1) % 3); break;
    case CMD_SWAP_COLORS: {
        Pixel color = primary;
        primary     = secondary;
        secondary   = color;
        break;
    }
    case CMD_PRIMARY:
    case CMD_SECONDARY:
    case CMD_CUSTOM_COLOR:
        color_secondary = id == CMD_SECONDARY || right;
        open_modal(MODAL_COLOR);
        break;
    case CMD_ABOUT: open_modal(MODAL_ABOUT); break;
    default: break;
    }
    invalidate_all();
}

Point App::bounded_point(int x, int y) const
{
    Point point = view.image_point(canvas, x, y);
    point.x     = clamp(point.x, 0, canvas.width - 1);
    point.y     = clamp(point.y, 0, canvas.height - 1);
    return point;
}

void App::paint_drag(Point point)
{
    Pixel color = drag_button == 3 ? secondary : primary;
    if (drag == DRAG_STROKE)
    {
        int size = tool == PENCIL ? 1 : brush_size;
        draw_stroke(canvas, last, point, tool == ERASER ? secondary : color, size);
        Rect dirty  = between(last, point);
        dirty.x    -= size;
        dirty.y    -= size;
        dirty.w    += size * 2;
        dirty.h    += size * 2;
        invalidate(view.screen_rect(canvas, intersect(dirty, canvas.bounds())));
    }
    else if (drag == DRAG_SHAPE)
    {
        history.restore(canvas);
        Point end = shift ? constrain_shape(start, point, tool) : point;
        draw_shape(canvas, tool, start, end, color, drag_button == 3 ? primary : secondary, brush_size, style);
        invalidate(view.area);
    }
    else if (drag == DRAG_SELECT)
    {
        selection = intersect(between(start, point), canvas.bounds());
        invalidate(view.area);
    }
    else if (drag == DRAG_MOVE)
    {
        history.restore(canvas);
        int x = clamp(move_source.x + point.x - start.x, 0, canvas.width - floating.width);
        int y = clamp(move_source.y + point.y - start.y, 0, canvas.height - floating.height);
        canvas.fill(move_source, secondary);
        paste_region(canvas, floating, x, y);
        selection = {x, y, floating.width, floating.height};
        invalidate(view.area);
    }
    last = point;
}

void App::pointer_down(int x, int y, int button)
{
    mouse = {x, y};
    layout();
    if (drag != DRAG_NONE) return;
    suppress_click  = false;
    pressed_control = hit_control(*this, x, y);
    if (pressed_control || modal != MODAL_NONE || menu >= 0) return;
    if (button == 1 && (horizontal_scroll.contains(x, y) || vertical_scroll.contains(x, y)))
    {
        drag           = horizontal_scroll.contains(x, y) ? DRAG_HSCROLL : DRAG_VSCROLL;
        pan_start      = mouse;
        scroll_start_x = view.scroll_x;
        scroll_start_y = view.scroll_y;
        drag_button    = 1;
        if (drag == DRAG_HSCROLL && !horizontal_thumb.contains(x, y))
            view.scroll_x += x < horizontal_thumb.x ? -view.area.w : view.area.w;
        if (drag == DRAG_VSCROLL && !vertical_thumb.contains(x, y))
            view.scroll_y += y < vertical_thumb.y ? -view.area.h : view.area.h;
        scroll_start_x = view.scroll_x;
        scroll_start_y = view.scroll_y;
        view.constrain(canvas);
        invalidate_all();
        return;
    }
    if (!view.area.contains(x, y)) return;
    if (button == 2 || tool == PAN || space)
    {
        drag           = DRAG_PAN;
        drag_button    = button;
        pan_start      = mouse;
        scroll_start_x = view.scroll_x;
        scroll_start_y = view.scroll_y;
        return;
    }
    Point point = view.image_point(canvas, x, y);
    if (!canvas.bounds().contains(point.x, point.y))
    {
        selection = {0, 0, 0, 0};
        invalidate(view.area);
        return;
    }
    start = last = point;
    drag_button  = button;
    if (tool == PICKER)
    {
        suppress_click = true;
        Pixel color    = canvas.get(point.x, point.y);
        // A picked transparent pixel becomes an opaque drawing color.
        if (button == 3)
            secondary = color | 0xff000000U;
        else
            primary = color | 0xff000000U;
        repaint_ui = true;
        return;
    }
    if (tool == SELECT)
    {
        if (button == 3) return;
        if (selection.contains(point.x, point.y))
        {
            if (!copy_region(canvas, selection, floating) || !begin_edit())
            {
                floating.reset();
                return;
            }
            move_source = selection;
            drag        = DRAG_MOVE;
        }
        else
        {
            drag      = DRAG_SELECT;
            selection = {point.x, point.y, 1, 1};
        }
        invalidate(view.area);
        return;
    }
    if (!begin_edit()) return;
    selection = {0, 0, 0, 0};
    if (tool == FILL)
    {
        suppress_click = true;
        Rect changed;
        if (!flood_fill(canvas, point, button == 3 ? secondary : primary, &changed))
        {
            history.cancel(canvas);
            set_status("内存不足，填充已取消", "Not enough memory; fill was cancelled");
        }
        else
            commit_edit();
        invalidate_all();
        return;
    }
    drag = tool == LINE || tool == RECTANGLE || tool == ELLIPSE ? DRAG_SHAPE : DRAG_STROKE;
    paint_drag(point);
}

void App::pointer_move(int x, int y)
{
    int hovered = hit_control(*this, mouse.x, mouse.y);
    mouse       = {x, y};
    if (hovered != hit_control(*this, x, y)) repaint_ui = true;
    repaint_status = true;
    if (drag == DRAG_NONE) return;
    if (drag == DRAG_PAN)
    {
        view.scroll_x = scroll_start_x - (x - pan_start.x);
        view.scroll_y = scroll_start_y - (y - pan_start.y);
        view.constrain(canvas);
        invalidate_all();
    }
    else if (drag == DRAG_HSCROLL || drag == DRAG_VSCROLL)
    {
        int range_x = maximum(0, (canvas.width * view.zoom + 99) / 100 - view.area.w);
        int range_y = maximum(0, (canvas.height * view.zoom + 99) / 100 - view.area.h);
        if (drag == DRAG_HSCROLL)
            view.scroll_x =
                scroll_start_x + (x - pan_start.x) * range_x / maximum(1, horizontal_scroll.w - horizontal_thumb.w);
        else
            view.scroll_y =
                scroll_start_y + (y - pan_start.y) * range_y / maximum(1, vertical_scroll.h - vertical_thumb.h);
        view.constrain(canvas);
        invalidate_all();
    }
    else
        paint_drag(bounded_point(x, y));
    // XAPI has a captured left release. Right/middle releases are hit-tested,
    // so finish these gestures when they leave the canvas viewport.
    if (drag_button != 1 && !view.area.contains(x, y)) pointer_up(x, y, drag_button);
}

void App::pointer_up(int x, int y, int button)
{
    if (drag == DRAG_NONE || drag_button != button) return;
    if (drag == DRAG_STROKE || drag == DRAG_SHAPE || drag == DRAG_SELECT || drag == DRAG_MOVE)
        paint_drag(bounded_point(x, y));
    if (history.active()) commit_edit();
    floating.reset();
    drag            = DRAG_NONE;
    drag_button     = 0;
    suppress_click  = true;
    pressed_control = 0;
    repaint_ui      = true;
}

void App::key(UINT64 code)
{
    if (modal != MODAL_NONE)
    {
        if (code == XKEY_ESC)
        {
            command(CMD_MODAL_CANCEL);
            return;
        }
        if (modal == MODAL_UNSAVED || modal == MODAL_OVERWRITE)
        {
            if (code == 's' || code == 'S' || code == XKEY_ENTER || code == '\n') command(CMD_MODAL_SAVE);
            if (modal == MODAL_UNSAVED && (code == 'd' || code == 'D')) command(CMD_MODAL_DISCARD);
            return;
        }
        if (code == XKEY_ENTER || code == '\n')
        {
            command(CMD_MODAL_OK);
            return;
        }
        if (code == XKEY_TAB || code == '\t')
        {
            if (modal != MODAL_COLOR) field = 1 - field;
            replace_input = true;
        }
        else if (code == XKEY_BACKSPACE || code == '\b')
        {
            size_t n = strlen(input[field]);
            if (replace_input)
                input[field][0] = '\0';
            else if (n)
                input[field][n - 1] = '\0';
            replace_input = false;
        }
        else if ((code >= '0' && code <= '9') ||
                 (modal == MODAL_COLOR && ((code >= 'a' && code <= 'f') || (code >= 'A' && code <= 'F'))))
        {
            size_t n = replace_input ? 0 : strlen(input[field]);
            if (n < (modal == MODAL_COLOR ? 6U : 4U))
            {
                input[field][n]     = (char)code;
                input[field][n + 1] = '\0';
                replace_input       = false;
            }
        }
        invalidate_all();
        return;
    }
    if (code == XKEY_ESC)
    {
        if (drag != DRAG_NONE)
            cancel_drag();
        else
        {
            menu      = -1;
            selection = {0, 0, 0, 0};
            invalidate_all();
        }
        return;
    }
    if (drag != DRAG_NONE) return;
    int ch = (int)code;
    if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    if (ctrl)
    {
        switch (ch)
        {
        case 'n': command(CMD_NEW); break;
        case 'o': command(CMD_OPEN); break;
        case 's': command(shift ? CMD_SAVE_AS : CMD_SAVE); break;
        case 'z': command(shift ? CMD_REDO : CMD_UNDO); break;
        case 'y': command(CMD_REDO); break;
        case 'c': command(CMD_COPY); break;
        case 'x': command(CMD_CUT); break;
        case 'v': command(CMD_PASTE); break;
        case 'a': command(CMD_SELECT_ALL); break;
        case '+':
        case '=': command(CMD_ZOOM_IN); break;
        case '-': command(CMD_ZOOM_OUT); break;
        case '0': command(CMD_FIT); break;
        default: break;
        }
        return;
    }
    switch (ch)
    {
    case 'p': command(CMD_TOOL + PENCIL); break;
    case 'b': command(CMD_TOOL + BRUSH); break;
    case 'e': command(CMD_TOOL + ERASER); break;
    case 'f': command(CMD_TOOL + FILL); break;
    case 'i': command(CMD_TOOL + PICKER); break;
    case 'l': command(CMD_TOOL + LINE); break;
    case 'r': command(CMD_TOOL + RECTANGLE); break;
    case 'o': command(CMD_TOOL + ELLIPSE); break;
    case 's': command(CMD_TOOL + SELECT); break;
    case 'h': command(CMD_TOOL + PAN); break;
    case 'x': command(CMD_SWAP_COLORS); break;
    case '[': command(CMD_SIZE_DOWN); break;
    case ']': command(CMD_SIZE_UP); break;
    case XKEY_DELETE: command(CMD_DELETE); break;
    case XKEY_LEFT: view.scroll_x -= 32; break;
    case XKEY_RIGHT: view.scroll_x += 32; break;
    case XKEY_UP: view.scroll_y -= 32; break;
    case XKEY_DOWN: view.scroll_y += 32; break;
    case XKEY_F1: command(CMD_ABOUT); break;
    default: return;
    }
    view.constrain(canvas);
    invalidate_all();
}

void App::message(UINT64 type, UINT64 high, UINT64 low)
{
    int x = (int)(INT64)high, y = (int)(INT64)low;
    switch (type)
    {
    case MSG_PASTE: receive_paste(high); break;
    case MSG_EDIT: {
        XClipInfo request;
        if (xapi_GetPasteInfo(low, &request) < 0 || request.window != window)
        {
            xapi_FinishPaste(low, 0);
            break;
        }
        if (high == XCLIP_EDIT_COPY)
            command(CMD_COPY);
        else if (high == XCLIP_EDIT_CUT)
            command(CMD_CUT);
        else if (high == XCLIP_EDIT_SELECT_ALL)
            command(CMD_SELECT_ALL);
        xapi_FinishPaste(low, 0);
        break;
    }
    case MSG_KEYDOWN:
        if (low == XKEY_CTRL)
            ctrl = true;
        else if (low == XKEY_SHIFT)
            shift = true;
        else if (low == XKEY_SPACE || low == ' ')
            space = true;
        else
            key(low);
        break;
    case MSG_KEYUP:
        if (low == XKEY_CTRL) ctrl = false;
        if (low == XKEY_SHIFT) shift = false;
        if (low == XKEY_SPACE || low == ' ') space = false;
        break;
    case MSG_MOVE: pointer_move(x, y); break;
    case MSG_LBUTTON_PRESS: pointer_down(x, y, 1); break;
    case MSG_RBUTTON_PRESS: pointer_down(x, y, 3); break;
    case MSG_MBUTTON_PRESS: pointer_down(x, y, 2); break;
    case MSG_LBUTTONUP: pointer_up(x, y, 1); break;
    case MSG_LBUTTON:
    case MSG_RBUTTON: {
        bool right = type == MSG_RBUTTON;
        if (drag != DRAG_NONE)
        {
            pointer_up(x, y, right ? 3 : 1);
            break;
        }
        layout();
        int id = hit_control(*this, x, y);
        if (suppress_click)
        {
            suppress_click  = false;
            pressed_control = 0;
            break;
        }
        if (id && id == pressed_control)
            command(id, right);
        else if (menu >= 0)
        {
            menu = -1;
            invalidate_all();
        }
        pressed_control = 0;
        break;
    }
    case MSG_MBUTTON: pointer_up(x, y, 2); break;
    case MSG_ROLLER:
        if (modal != MODAL_NONE || menu >= 0 || drag != DRAG_NONE || x == 0) break;
        if (ctrl)
            zoom(view.zoom + (x > 0 ? (view.zoom >= 100 ? 100 : 25) : (view.zoom > 100 ? -100 : -25)));
        else
        {
            if (shift)
                view.scroll_x -= clamp(x, -8, 8) * 48;
            else
                view.scroll_y -= clamp(x, -8, 8) * 48;
            view.constrain(canvas);
            invalidate_all();
        }
        break;
    case MSG_RESIZE:
        cancel_drag();
        width  = clamp(x, 1, 16384);
        height = clamp(y, 1, 16384);
        layout();
        invalidate_all();
        break;
    default: break;
    }
}

void App::render()
{
    if (!repaint_ui && !repaint_view && !repaint_status) return;
    if (repaint_ui)
    {
        layout();
        draw_ui(*this);
        repaint_view = true;
        dirty_region = view.area;
    }
    if (repaint_view)
    {
        Rect region = dirty_region.empty() ? view.area : dirty_region;
        draw_canvas(*this, region);
    }
    extern void draw_status(App & app);
    if (repaint_status && !repaint_ui) draw_status(*this);
    // Menus and modal dialogs are painted last and never enter the image buffer.
    extern void draw_overlays(App & app);
    draw_overlays(*this);
    xapi_RefreshWindow(window);
    repaint_ui = repaint_view = repaint_status = false;
    dirty_region                               = {0, 0, 0, 0};
}

} // namespace xphoton
