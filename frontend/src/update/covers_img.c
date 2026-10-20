/*
 * covers_img.c - the image part of rsos-covers: decode the server's PNG,
 * scale it to fit, write a JPEG. Built with the vendored stb headers in their
 * own translation unit (their style, not our extra warnings: update.mk).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "../../third_party/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../third_party/stb/stb_image_write.h"

#include "covers_img.h"

/* Box filter: every output pixel averages the input pixels it covers. */
static unsigned char *scale(const unsigned char *src, int w, int h, int nw, int nh)
{
	unsigned char *dst = malloc((size_t)nw * nh * 3);

	if (!dst)
		return NULL;
	for (int y = 0; y < nh; y++) {
		int y0 = y * h / nh, y1 = (y + 1) * h / nh;

		if (y1 <= y0)
			y1 = y0 + 1;
		for (int x = 0; x < nw; x++) {
			int x0 = x * w / nw, x1 = (x + 1) * w / nw;
			unsigned sum[3] = { 0, 0, 0 }, n = 0;

			if (x1 <= x0)
				x1 = x0 + 1;
			for (int yy = y0; yy < y1 && yy < h; yy++)
				for (int xx = x0; xx < x1 && xx < w; xx++) {
					const unsigned char *p = src + ((size_t)yy * w + xx) * 3;

					sum[0] += p[0];
					sum[1] += p[1];
					sum[2] += p[2];
					n++;
				}
			for (int c = 0; c < 3; c++)
				dst[((size_t)y * nw + x) * 3 + c] = (unsigned char)(n ? sum[c] / n : 0);
		}
	}
	return dst;
}

int covers_img_write(const void *data, size_t len, int max, int quality, const char *path)
{
	int w, h, n, nw, nh, ok;
	unsigned char *px, *out;

	if (len > 64u * 1024 * 1024)
		return -1;
	px = stbi_load_from_memory(data, (int)len, &w, &h, &n, 3);
	if (!px)
		return -1;
	nw = w;
	nh = h;
	if (w > max || h > max) {
		if (w >= h) {
			nw = max;
			nh = (int)((int64_t)h * max / w);
		} else {
			nh = max;
			nw = (int)((int64_t)w * max / h);
		}
		if (nw < 1)
			nw = 1;
		if (nh < 1)
			nh = 1;
	}
	out = (nw == w && nh == h) ? px : scale(px, w, h, nw, nh);
	if (!out) {
		stbi_image_free(px);
		return -1;
	}
	ok = stbi_write_jpg(path, nw, nh, 3, out, quality);
	if (out != px)
		free(out);
	stbi_image_free(px);
	return ok ? 0 : -1;
}
