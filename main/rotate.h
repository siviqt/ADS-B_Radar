#pragma once
#include <stdint.h>

// Packed RGB565 rectangle, clockwise into a packed h-by-w rectangle.
static inline void adsb_rotate565(uint16_t *dst, const uint16_t *src, int w, int h) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            dst[x * h + h - 1 - y] = src[y * w + x];
}
