#pragma once

// Keep the vendored codec unchanged. The freestanding app needs only its C
// encoder and memory API, with all file I/O handled through XAPI POSIX wrappers.
#define LODEPNG_NO_COMPILE_CPP
#define LODEPNG_NO_COMPILE_DISK
#define LODEPNG_NO_COMPILE_DECODER
#define LODEPNG_NO_COMPILE_ANCILLARY_CHUNKS
#define LODEPNG_MAX_ALLOC (128ULL * 1024 * 1024)
#include "../../third_party/litehtml/containers/test/lodepng.h"
