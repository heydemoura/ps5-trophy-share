/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */
#include <stdlib.h>
#include <string.h>

#include "icon.h"

/* Declarations only; the code is built in stb_impl.c. */
#define STBI_NO_STDIO
#define STBI_WRITE_NO_STDIO
#include "stb_image.h"
#include "stb_image_write.h"

#define JPEG_QUALITY 82

typedef struct {
  unsigned char *p;
  size_t len, cap;
  int bad;
} out_t;

static void out_write(void *ctx, void *data, int size) {
  out_t *o = ctx;
  if (o->bad || size <= 0) return;
  if (o->len + (size_t)size > o->cap) {
    size_t cap = o->cap ? o->cap * 2 : 32 * 1024;
    unsigned char *p;
    while (cap < o->len + (size_t)size) cap *= 2;
    if (!(p = realloc(o->p, cap))) { o->bad = 1; return; }
    o->p = p;
    o->cap = cap;
  }
  memcpy(o->p + o->len, data, (size_t)size);
  o->len += (size_t)size;
}

/* Shrinks RGB pixels by averaging every source pixel that falls into a target pixel. */
static unsigned char *shrink(const unsigned char *src, int w, int h, int tw, int th) {
  unsigned char *dst = malloc((size_t)tw * (size_t)th * 3);
  if (!dst) return NULL;
  for (int y = 0; y < th; y++) {
    int y0 = (int)((long long)y * h / th), y1 = (int)((long long)(y + 1) * h / th);
    if (y1 <= y0) y1 = y0 + 1;
    for (int x = 0; x < tw; x++) {
      int x0 = (int)((long long)x * w / tw), x1 = (int)((long long)(x + 1) * w / tw);
      unsigned long r = 0, g = 0, b = 0, n;
      if (x1 <= x0) x1 = x0 + 1;
      for (int sy = y0; sy < y1; sy++) {
        const unsigned char *p = src + ((size_t)sy * (size_t)w + (size_t)x0) * 3;
        for (int sx = x0; sx < x1; sx++, p += 3) {
          r += p[0];
          g += p[1];
          b += p[2];
        }
      }
      n = (unsigned long)(y1 - y0) * (unsigned long)(x1 - x0);
      dst[((size_t)y * (size_t)tw + (size_t)x) * 3 + 0] = (unsigned char)((r + n / 2) / n);
      dst[((size_t)y * (size_t)tw + (size_t)x) * 3 + 1] = (unsigned char)((g + n / 2) / n);
      dst[((size_t)y * (size_t)tw + (size_t)x) * 3 + 2] = (unsigned char)((b + n / 2) / n);
    }
  }
  return dst;
}

int icon_make(const unsigned char *png, size_t png_len, int max_side, unsigned char **jpg, size_t *jpg_len) {
  unsigned char *rgb, *small = NULL;
  const unsigned char *use;
  out_t out = {0};
  int w = 0, h = 0, n = 0, tw, th;

  *jpg = NULL;
  *jpg_len = 0;
  if (!png || png_len < 8 || png_len > 16u * 1024u * 1024u || max_side < 8) return -1;
  if (!(rgb = stbi_load_from_memory(png, (int)png_len, &w, &h, &n, 3))) return -1;
  if (w < 1 || h < 1) { stbi_image_free(rgb); return -1; }

  /* Keep the shape; never enlarge. */
  tw = w;
  th = h;
  if (w >= h && w > max_side) { tw = max_side; th = (int)((long long)h * max_side / w); }
  else if (h > w && h > max_side) { th = max_side; tw = (int)((long long)w * max_side / h); }
  if (tw < 1) tw = 1;
  if (th < 1) th = 1;

  use = rgb;
  if (tw != w || th != h) {
    if (!(small = shrink(rgb, w, h, tw, th))) { stbi_image_free(rgb); return -1; }
    use = small;
  }
  if (!stbi_write_jpg_to_func(out_write, &out, tw, th, 3, use, JPEG_QUALITY) || out.bad || !out.len) {
    free(out.p);
    out.p = NULL;
  }
  free(small);
  stbi_image_free(rgb);
  if (!out.p) return -1;
  *jpg = out.p;
  *jpg_len = out.len;
  return 0;
}
