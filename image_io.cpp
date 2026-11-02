#include "image_io.h"
#include "png_codec.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace xphoton {

bool has_extension(const char *path, const char *extension)
{
    if (!path || !extension) return false;
    size_t n = strlen(path), m = strlen(extension);
    if (m > n) return false;
    for (size_t i = 0; i < m; ++i)
    {
        char a = path[n - m + i], b = extension[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (a != b) return false;
    }
    return true;
}

bool writable_format(const char *path)
{
    return has_extension(path, ".png") || has_extension(path, ".bmp");
}

static void put16(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
}

static void put32(unsigned char *data, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        data[i] = (unsigned char)(value >> (i * 8));
}

IoResult encode_image(const Canvas &canvas, bool bmp, unsigned char **bytes, size_t *length)
{
    *bytes  = NULL;
    *length = 0;
    if (!canvas.pixels || !Canvas::valid_size(canvas.width, canvas.height)) return IO_MEMORY;
    if (!bmp)
    {
        unsigned error = lodepng_encode32(bytes, length, (const unsigned char *)canvas.pixels, (unsigned)canvas.width,
                                          (unsigned)canvas.height);
        if (error)
        {
            free(*bytes);
            *bytes  = NULL;
            *length = 0;
            return error == 83 ? IO_MEMORY : IO_ENCODE;
        }
        return IO_OK;
    }

    // BITMAPV4HEADER with explicit RGBA masks preserves transparency, unlike a
    // 24-bit export. Every row is naturally aligned to four bytes.
    size_t payload      = (size_t)canvas.width * canvas.height * 4;
    *length             = 122 + payload;
    unsigned char *data = (unsigned char *)malloc(*length);
    if (!data)
    {
        *length = 0;
        return IO_MEMORY;
    }
    memset(data, 0, 122);
    data[0] = 'B';
    data[1] = 'M';
    put32(data + 2, (uint32_t)*length);
    put32(data + 10, 122);
    put32(data + 14, 108);
    put32(data + 18, (unsigned)canvas.width);
    put32(data + 22, (unsigned)canvas.height);
    put16(data + 26, 1);
    put16(data + 28, 32);
    put32(data + 30, 3);
    put32(data + 34, (uint32_t)payload);
    put32(data + 54, 0x00ff0000);
    put32(data + 58, 0x0000ff00);
    put32(data + 62, 0x000000ff);
    put32(data + 66, 0xff000000);
    put32(data + 70, 0x73524742); // LCS_sRGB
    for (int y = 0; y < canvas.height; ++y)
        for (int x = 0; x < canvas.width; ++x)
        {
            Pixel  pixel = canvas.get(x, canvas.height - 1 - y);
            size_t i     = 122 + ((size_t)y * canvas.width + x) * 4;
            data[i]      = (unsigned char)(pixel >> 16);
            data[i + 1]  = (unsigned char)(pixel >> 8);
            data[i + 2]  = (unsigned char)pixel;
            data[i + 3]  = (unsigned char)(pixel >> 24);
        }
    *bytes = data;
    return IO_OK;
}

static int unique_sibling(const char *path, const char *suffix, char *output, size_t capacity)
{
    static unsigned sequence;
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        int n = snprintf(output, capacity, "%s.xphoton-%u-%u.%s", path, (unsigned)getpid(), ++sequence, suffix);
        if (n < 0 || (size_t)n >= capacity)
        {
            errno = ENAMETOOLONG;
            return -1;
        }
        int fd = open(output, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd >= 0 || errno != EEXIST) return fd;
    }
    errno = EEXIST;
    return -1;
}

static bool write_all(int fd, const unsigned char *data, size_t length)
{
    while (length)
    {
        ssize_t result = write(fd, data, length);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return false;
        data   += result;
        length -= (size_t)result;
    }
    return true;
}

IoResult save_image(const Canvas &canvas, const char *path, char *recovery_path, size_t recovery_capacity)
{
    if (recovery_path && recovery_capacity) recovery_path[0] = '\0';
    if (!path || !path[0] || strlen(path) > 480) return IO_PATH;
    if (!writable_format(path)) return IO_FORMAT;
    struct stat st;
    bool        exists = stat(path, &st) == 0;
    if (exists && !S_ISREG(st.st_mode)) return IO_PATH;
    if (!exists && errno != ENOENT) return IO_WRITE;

    unsigned char *bytes  = NULL;
    size_t         length = 0;
    IoResult       result = encode_image(canvas, has_extension(path, ".bmp"), &bytes, &length);
    if (result != IO_OK) return result;
    char temporary[576], backup[576];
    int  fd = unique_sibling(path, "tmp", temporary, sizeof(temporary));
    if (fd < 0)
    {
        free(bytes);
        return IO_WRITE;
    }
    bool written = write_all(fd, bytes, length);
    free(bytes);
    if (written && fsync(fd) != 0) written = false;
    if (close(fd) != 0) written = false;
    if (!written)
    {
        unlink(temporary);
        return IO_WRITE;
    }

    // XJ380 rename removes an existing target before moving the source. Keep
    // the old file under a reserved sibling name until replacement succeeds.
    if (exists)
    {
        int reserved = unique_sibling(path, "bak", backup, sizeof(backup));
        if (reserved < 0)
        {
            unlink(temporary);
            return IO_WRITE;
        }
        if (close(reserved) != 0 || rename(path, backup) != 0)
        {
            unlink(backup);
            unlink(temporary);
            return IO_WRITE;
        }
    }
    if (rename(temporary, path) != 0)
    {
        if (exists && rename(backup, path) != 0)
        {
            if (recovery_path && recovery_capacity) snprintf(recovery_path, recovery_capacity, "%s", backup);
            return IO_RECOVERY;
        }
        unlink(temporary);
        return IO_WRITE;
    }
    if (exists) unlink(backup);
    return IO_OK;
}

} // namespace xphoton
