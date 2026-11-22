/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ucp.h"

#define UCP_MAGIC 0xb228c60au
#define UCP_TABLE 0x60
#define UCP_MAX_FILES 4096

static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t be64(const uint8_t *p) {
  return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

int ucp_list(const char *path, ucp_entry_t **entries, int *count) {
  uint8_t head[0x18], row[0x100];
  uint32_t n, row_sz, i;
  ucp_entry_t *list = NULL;
  FILE *f;

  *entries = NULL;
  *count = 0;
  if (!(f = fopen(path, "rb"))) return -1;
  if (fread(head, 1, sizeof(head), f) != sizeof(head) || be32(head) != UCP_MAGIC) goto fail;
  n = be32(head + 0x10);
  row_sz = be32(head + 0x14);
  if (n == 0 || n > UCP_MAX_FILES || row_sz < 0x30 || row_sz > sizeof(row)) goto fail;
  if (!(list = calloc(n, sizeof(*list))) || fseek(f, UCP_TABLE, SEEK_SET) != 0) goto fail;
  for (i = 0; i < n; i++) {
    if (fread(row, 1, row_sz, f) != row_sz) goto fail;
    memcpy(list[i].name, row, 32);
    list[i].name[32] = 0;
    list[i].off = be64(row + 0x20);
    list[i].size = be64(row + 0x28);
  }
  fclose(f);
  *entries = list;
  *count = (int)n;
  return 0;
fail:
  free(list);
  fclose(f);
  return -1;
}

const ucp_entry_t *ucp_find(const ucp_entry_t *entries, int count, const char *name) {
  for (int i = 0; i < count; i++)
    if (!strcmp(entries[i].name, name)) return &entries[i];
  return NULL;
}

int ucp_read(const char *path, const ucp_entry_t *e, size_t max, char **buf, size_t *len) {
  char *p;
  FILE *f;

  *buf = NULL;
  *len = 0;
  if (!e || e->size == 0 || e->size > max) return -1;
  if (!(f = fopen(path, "rb"))) return -1;
  if (!(p = malloc((size_t)e->size + 1))) { fclose(f); return -1; }
  if (fseek(f, (long)e->off, SEEK_SET) != 0 || fread(p, 1, (size_t)e->size, f) != (size_t)e->size) {
    free(p);
    fclose(f);
    return -1;
  }
  fclose(f);
  p[e->size] = 0;
  *buf = p;
  *len = (size_t)e->size;
  return 0;
}
