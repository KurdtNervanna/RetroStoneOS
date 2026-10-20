/*
 * covers_img.h - rsos-covers' image step (covers_img.c).
 */
#ifndef RSOS_COVERS_IMG_H
#define RSOS_COVERS_IMG_H

#include <stddef.h>

/* Decodes a PNG/JPEG in memory, scales it to fit max x max (aspect kept,
 * never enlarged) and writes a JPEG of that quality to path. 0, or -1 when
 * the image cannot be decoded or written. */
int covers_img_write(const void *data, size_t len, int max, int quality, const char *path);

#endif
