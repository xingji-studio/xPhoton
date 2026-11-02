#pragma once

#include "canvas.h"

namespace xphoton {

class History {
  public:
    History();
    ~History();
    bool begin(const Canvas &canvas);
    bool restore(Canvas &canvas) const;
    void cancel(Canvas &canvas);
    // Commit is transactional: allocation failure restores the original image.
    bool commit(Canvas &canvas, Rect *changed = NULL);
    bool undo(Canvas &canvas);
    bool redo(Canvas &canvas);
    bool can_undo() const
    {
        return applied != NULL;
    }
    bool can_redo() const;
    bool active() const
    {
        return snapshot.pixels != NULL;
    }
    bool dirty() const
    {
        return state != saved_state;
    }
    void mark_saved()
    {
        saved_state = state;
    }
    void   reset();
    size_t bytes_used() const
    {
        return used;
    }

  private:
    struct Entry
    {
        Entry   *prev, *next;
        Pixel   *data;
        Rect     rect;
        int      width, height;
        bool     resized;
        size_t   cost;
        uint64_t before, after;
    };

    Canvas   snapshot;
    Entry   *first, *last, *applied;
    size_t   used;
    unsigned count;
    uint64_t state, saved_state, next_state;

    void drop(Entry *entry);
    void apply(Canvas &canvas, Entry *entry);
    History(const History &);
    History &operator=(const History &);
};

} // namespace xphoton
