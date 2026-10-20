/* Leitor de PNG (png.c). */
#ifndef PM_PNG_H
#define PM_PNG_H

#include <stddef.h>
#include <stdint.h>

/* Decodifica um PNG da memória para ARGB (0xAARRGGBB); NULL se não conseguir.
 * Libere com free(). */
uint32_t *png_decode(const uint8_t *data, size_t size, int *w, int *h);

#endif
