/* the WebAssembly build has no libc: what minimp3 needs from <string.h> (the SDK brings the functions) */
#ifndef VIBE_FREESTANDING_STRING_H
#define VIBE_FREESTANDING_STRING_H
#include <stddef.h>
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
#endif
