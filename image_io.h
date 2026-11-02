#pragma once

#include "canvas.h"

namespace xphoton {

enum IoResult
{
    IO_OK,
    IO_PATH,
    IO_FORMAT,
    IO_MEMORY,
    IO_ENCODE,
    IO_WRITE,
    IO_RECOVERY
};

bool     has_extension(const char *path, const char *extension);
bool     writable_format(const char *path);
IoResult encode_image(const Canvas &canvas, bool bmp, unsigned char **bytes, size_t *length);
// Writes a complete sibling temporary file before replacing the destination.
// recovery_path identifies a retained original if replacement rollback fails.
IoResult save_image(const Canvas &canvas, const char *path, char *recovery_path, size_t recovery_capacity);

} // namespace xphoton
