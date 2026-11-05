#pragma once
#include <stdbool.h>
#include <stddef.h>
// Subtraction-based bounds avoid address+length overflow. Empty writes are harmless.
static inline bool adsb_nvs_write_in_bounds(size_t address, size_t length,
                                           size_t start, size_t size) {
    return address >= start && address - start <= size && length <= size - (address - start);
}
