#pragma once

#include "canvas.h"
#include "history.h"
#include "tools.h"
#include <x3api.h>
#include <xclipboard.h>

namespace xphoton {

enum Command
{
    CMD_NONE = 0,
    CMD_NEW  = 100,
    CMD_OPEN,
    CMD_SAVE,
    CMD_SAVE_AS,
    CMD_EXIT,
    CMD_UNDO,
    CMD_REDO,
    CMD_CUT,
    CMD_COPY,
    CMD_PASTE,
    CMD_DELETE,
    CMD_SELECT_ALL,
    CMD_DESELECT,
    CMD_CROP,
    CMD_RESIZE_CANVAS,
    CMD_RESIZE_IMAGE,
    CMD_ROTATE_RIGHT,
    CMD_ROTATE_LEFT,
    CMD_FLIP_H,
    CMD_FLIP_V,
    CMD_ZOOM_OUT,
    CMD_ZOOM_IN,
    CMD_ACTUAL_SIZE,
    CMD_FIT,
    CMD_ABOUT,
    CMD_SIZE_DOWN,
    CMD_SIZE_UP,
    CMD_STYLE,
    CMD_SWAP_COLORS,
    CMD_CUSTOM_COLOR,
    CMD_PRIMARY,
    CMD_SECONDARY,
    CMD_TOOL       = 200,
    CMD_PALETTE    = 300,
    CMD_MENU       = 400,
    CMD_MODAL_SAVE = 500,
    CMD_MODAL_DISCARD,
    CMD_MODAL_CANCEL,
    CMD_MODAL_OK,
    CMD_FIELD_ONE,
    CMD_FIELD_TWO
};

enum Modal
{
    MODAL_NONE,
    MODAL_UNSAVED,
    MODAL_NEW,
    MODAL_CANVAS_SIZE,
    MODAL_IMAGE_SIZE,
    MODAL_COLOR,
    MODAL_ABOUT,
    MODAL_OVERWRITE
};
enum Drag
{
    DRAG_NONE,
    DRAG_STROKE,
    DRAG_SHAPE,
    DRAG_SELECT,
    DRAG_MOVE,
    DRAG_PAN,
    DRAG_HSCROLL,
    DRAG_VSCROLL
};

struct Control
{
    int         id;
    Rect        rect;
    const char *zh, *en, *icon;
    bool        enabled, selected;
};

class App {
  public:
    HDLE       window;
    Canvas     canvas, clipboard, floating;
    History    history;
    Viewport   view;
    Tool       tool;
    ShapeStyle style;
    Pixel      primary, secondary;
    int        brush_size, width, height, language;
    bool       running, ctrl, shift, space, repaint_ui, repaint_view, repaint_status, suppress_click;
    int        pressed_control;
    Rect       dirty_region, selection;
    Point      mouse, start, last, pan_start;
    Drag       drag;
    int        drag_button, scroll_start_x, scroll_start_y;
    Rect       move_source;
    Modal      modal;
    int        pending_command, menu, field;
    char       input[2][32];
    bool       replace_input;
    bool       color_secondary;
    char       path[512], pending_path[512], status[192];
    Control    controls[128];
    int        control_count;
    Rect       horizontal_scroll, vertical_scroll, horizontal_thumb, vertical_thumb;

    App();
    bool        initialize();
    bool        load(const char *filename);
    bool        save(bool save_as);
    void        command(int id, bool right = false);
    void        message(UINT64 type, UINT64 high, UINT64 low);
    void        cancel_drag();
    void        invalidate(Rect rect);
    void        invalidate_all();
    void        set_status(const char *zh, const char *en);
    const char *tr(const char *zh, const char *en) const;
    void        update_title();
    void        render();
    void        layout();
    void        input_lost();
    void        receive_paste(UINT64 paste_id);

  private:
    bool  write_to(const char *filename);
    bool  begin_edit();
    void  commit_edit();
    void  request_document_command(int id);
    void  execute_document_command(int id);
    void  open_modal(Modal kind);
    void  accept_modal();
    void  key(UINT64 code);
    void  pointer_down(int x, int y, int button);
    void  pointer_move(int x, int y);
    void  pointer_up(int x, int y, int button);
    void  paint_drag(Point point);
    Point bounded_point(int x, int y) const;
    void  zoom(int value);
    void  copy_selection(bool cut);
    void  paste();
    void  delete_selection();
    void  transform(Transform kind);
};

// The message callback must keep draining during the system file dialog.
// This discards input queued for the editor while another window owned focus.
void        discard_input();
void        draw_ui(App &app);
void        draw_canvas(App &app, Rect region);
int         hit_control(const App &app, int x, int y);
const char *tool_name(const App &app, Tool tool);

} // namespace xphoton
