#include "history.h"
#include <stdlib.h>
#include <string.h>

namespace xphoton {

static const size_t   HISTORY_BUDGET = 64ULL * 1024 * 1024;
static const unsigned HISTORY_LIMIT  = 128;

History::History() : first(NULL), last(NULL), applied(NULL), used(0), count(0), state(1), saved_state(1), next_state(2)
{
}

History::~History()
{
    reset();
}

void History::drop(Entry *entry)
{
    if (entry->prev)
        entry->prev->next = entry->next;
    else
        first = entry->next;
    if (entry->next)
        entry->next->prev = entry->prev;
    else
        last = entry->prev;
    used -= entry->cost;
    --count;
    free(entry->data);
    free(entry);
}

void History::reset()
{
    while (first)
        drop(first);
    applied = NULL;
    snapshot.reset();
    state = saved_state = 1;
    next_state          = 2;
}

bool History::begin(const Canvas &canvas)
{
    return !active() && snapshot.copy(canvas);
}

bool History::restore(Canvas &canvas) const
{
    return active() && canvas.copy(snapshot);
}

void History::cancel(Canvas &canvas)
{
    if (!active()) return;
    snapshot.swap(canvas);
    snapshot.reset();
}

bool History::commit(Canvas &canvas, Rect *changed)
{
    if (changed) *changed = {0, 0, 0, 0};
    if (!active()) return false;
    bool resized = snapshot.width != canvas.width || snapshot.height != canvas.height;
    Rect rect    = canvas.bounds();
    if (!resized)
    {
        int left = canvas.width, top = canvas.height, right = -1, bottom = -1;
        for (int y = 0; y < canvas.height; ++y)
        {
            for (int x = 0; x < canvas.width; ++x)
            {
                size_t i = (size_t)y * canvas.width + x;
                if (canvas.pixels[i] == snapshot.pixels[i]) continue;
                left   = minimum(left, x);
                top    = minimum(top, y);
                right  = maximum(right, x);
                bottom = y;
            }
        }
        if (right < left)
        {
            snapshot.reset();
            return true;
        }
        rect = {left, top, right - left + 1, bottom - top + 1};
    }

    size_t cost = sizeof(Entry) + (resized ? maximum(snapshot.width * snapshot.height, canvas.width * canvas.height)
                                           : (size_t)rect.w * rect.h) *
                                      sizeof(Pixel);
    Entry *entry = cost <= HISTORY_BUDGET ? (Entry *)malloc(sizeof(Entry)) : NULL;
    Pixel *data  = NULL;
    if (entry && !resized) data = (Pixel *)malloc((size_t)rect.w * rect.h * sizeof(Pixel));
    if (!entry || (!resized && !data))
    {
        free(entry);
        cancel(canvas);
        return false;
    }

    memset(entry, 0, sizeof(*entry));
    entry->resized = resized;
    entry->width   = snapshot.width;
    entry->height  = snapshot.height;
    entry->rect    = rect;
    entry->cost    = cost;
    entry->before  = state;
    entry->after   = next_state++;
    if (resized)
    {
        entry->data     = snapshot.pixels;
        snapshot.pixels = NULL;
        snapshot.width = snapshot.height = 0;
    }
    else
    {
        // An XOR patch is its own inverse: history stores one changed region,
        // rather than two full images for every stroke.
        entry->data = data;
        for (int y = 0; y < rect.h; ++y)
            for (int x = 0; x < rect.w; ++x)
            {
                size_t i                     = (size_t)(rect.y + y) * canvas.width + rect.x + x;
                data[(size_t)y * rect.w + x] = snapshot.pixels[i] ^ canvas.pixels[i];
            }
        snapshot.reset();
    }
    while (last && last != applied)
        drop(last);
    while (first && (used + cost > HISTORY_BUDGET || count >= HISTORY_LIMIT))
        drop(first);
    entry->prev = last;
    if (last)
        last->next = entry;
    else
        first = entry;
    last = applied  = entry;
    used           += cost;
    ++count;
    state = entry->after;
    if (changed) *changed = rect;
    return true;
}

void History::apply(Canvas &canvas, Entry *entry)
{
    if (entry->resized)
    {
        Pixel *data = canvas.pixels;
        int    w = canvas.width, h = canvas.height;
        canvas.pixels = entry->data;
        canvas.width  = entry->width;
        canvas.height = entry->height;
        entry->data   = data;
        entry->width  = w;
        entry->height = h;
        return;
    }
    for (int y = 0; y < entry->rect.h; ++y)
        for (int x = 0; x < entry->rect.w; ++x)
            canvas.pixels[(size_t)(entry->rect.y + y) * canvas.width + entry->rect.x + x] ^=
                entry->data[(size_t)y * entry->rect.w + x];
}

bool History::can_redo() const
{
    return applied ? applied->next != NULL : first != NULL;
}

bool History::undo(Canvas &canvas)
{
    if (active() || !applied) return false;
    Entry *entry = applied;
    apply(canvas, entry);
    state   = entry->before;
    applied = entry->prev;
    return true;
}

bool History::redo(Canvas &canvas)
{
    if (active()) return false;
    Entry *entry = applied ? applied->next : first;
    if (!entry) return false;
    apply(canvas, entry);
    state   = entry->after;
    applied = entry;
    return true;
}

} // namespace xphoton
