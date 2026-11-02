#include "app.h"
#include "image_io.h"

namespace xphoton {

struct Message
{
    UINT64 type, high, low;
};
static const unsigned QUEUE_SIZE = 1024;
static Message        queue[QUEUE_SIZE];
static unsigned       read_index, write_index;
static bool           overflow;

static void receive_message(UINT64 type, UINT64 high, UINT64 low)
{
    // Legacy CHAR/SPCHAR messages duplicate KEYDOWN. Processing only the raw
    // key edges also keeps modifier handling independent of text input modes.
    if (type != MSG_PASTE && type != MSG_EDIT && type != MSG_KEYDOWN && type != MSG_KEYUP && type != MSG_MOVE &&
        type != MSG_RESIZE && type != MSG_LBUTTON_PRESS && type != MSG_RBUTTON_PRESS && type != MSG_MBUTTON_PRESS &&
        type != MSG_LBUTTON && type != MSG_RBUTTON && type != MSG_MBUTTON && type != MSG_LBUTTONUP &&
        type != MSG_ROLLER)
        return;
    unsigned write = __atomic_load_n(&write_index, __ATOMIC_RELAXED);
    unsigned next  = (write + 1) % QUEUE_SIZE;
    if (next == __atomic_load_n(&read_index, __ATOMIC_ACQUIRE))
    {
        // Never block the XAPI callback: a system file dialog can temporarily
        // pause the editor loop. Overflow cancels the gesture on the main loop.
        __atomic_store_n(&overflow, true, __ATOMIC_RELEASE);
        return;
    }
    queue[write] = {type, high, low};
    __atomic_store_n(&write_index, next, __ATOMIC_RELEASE);
}

void discard_input()
{
    __atomic_store_n(&read_index, __atomic_load_n(&write_index, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
    __atomic_store_n(&overflow, false, __ATOMIC_RELEASE);
}

static void process_messages(App &app)
{
    for (unsigned count = 0; count < 256 && app.running; ++count)
    {
        if (__atomic_exchange_n(&overflow, false, __ATOMIC_ACQ_REL))
        {
            discard_input();
            app.input_lost();
            return;
        }
        unsigned read = __atomic_load_n(&read_index, __ATOMIC_RELAXED);
        if (read == __atomic_load_n(&write_index, __ATOMIC_ACQUIRE)) break;
        Message message = queue[read];
        __atomic_store_n(&read_index, (read + 1) % QUEUE_SIZE, __ATOMIC_RELEASE);
        app.message(message.type, message.high, message.low);
    }
}

} // namespace xphoton

extern "C" int main(int argc, char *argv[], char *envp[])
{
    (void)envp;
    xphoton::App app;
    if (!app.initialize()) return 1;
    XWINDOW window       = {1024, 720, (char *)"xPhoton", XWIN_NORMAL | XWIN_SUPPORT_RESIZEABLE};
    UINT32  screen_width = 0, screen_height = 0;
    // A metadata-only query returns dimensions without capturing any pixels.
    if (xapi_CaptureScreen(NULL, 0, &screen_width, &screen_height) == 0 && screen_width >= 320 && screen_height >= 240)
    {
        window.width  = xphoton::minimum(1024, (int)screen_width - 100);
        window.height = xphoton::minimum(720, (int)screen_height - 170);
    }
    xapi_CreateWindow(&app.window, &window);
    if (!app.window) return 1;
    xapi_SetIcon(app.window, (char *)"/system/resources/icon/xphoton.png");
    UINT64 width = 0, height = 0;
    xapi_GetWindowSize(app.window, &width, &height);
    if (width && height)
    {
        app.width  = (int)width;
        app.height = (int)height;
    }
    app.layout();
    app.view.fit(app.canvas);
    SetMsgPrcor(app.window, xphoton::receive_message);
    xapi_ClipboardAttach(app.window, XCLIP_STANDARD, XCLIP_FORMAT_BIT(XCLIP_RGBA8));
    // Numeric/hexadecimal dialogs need physical keys, not an IME composition.
    XImeContext context = {};
    context.size        = sizeof(context);
    context.version     = XIME_ABI_VERSION;
    context.window      = app.window;
    context.control     = 1;
    xapi_ImeSetContext(&context);
    const char *filename = argc > 1 && argv ? argv[1] : argc == 1 && argv ? argv[0] : NULL;
    if (filename && filename[0] && !xphoton::has_extension(filename, ".elf")) app.load(filename);
    app.update_title();
    app.invalidate_all();
    while (app.running)
    {
        xphoton::process_messages(app);
        if (app.running) app.render();
        xapi_Sleep(12);
    }
    return 0;
}
