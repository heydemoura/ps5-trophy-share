/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */
#include <stdlib.h>
#include <string.h>

#include "t2pd.h"

#define HEADER 0x40        /* "T2PD" header, not hashed */
#define HASH 0x20          /* each block ends with a 32-byte hash of its data */
#define ENTRY 0x20         /* one row of the table of contents */
#define REC_HEAD 0x10      /* every record repeats its type and size */
#define TYPE_STATE 0x800   /* one record per trophy: earned or not, and when */

/* Microseconds from 0001-01-01 to 1970-01-01. */
#define EPOCH_US 62135596800000000LL

static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t be64(const uint8_t *p) {
  return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

int t2pd_parse(const uint8_t *data, size_t len, t2pd_t *out) {
  uint64_t block, blocks, i;
  uint8_t *flat;
  size_t flat_len, part;
  uint32_t entries;
  int rc = -1;

  memset(out, 0, sizeof(*out));
  if (len < HEADER || memcmp(data, "T2PD", 4) != 0) return -1;
  block = be64(data + 0x08);
  blocks = be64(data + 0x20);
  if (block <= HASH || block > 0x10000 || blocks == 0 || blocks > 4096) return -1;
  if (HEADER + blocks * block > len) return -1;

  /* Drop the hashes: what is left is one continuous stream that offsets refer to. */
  part = (size_t)block - HASH;
  flat_len = (size_t)blocks * part;
  if (!(flat = malloc(flat_len))) return -1;
  for (i = 0; i < blocks; i++) memcpy(flat + i * part, data + HEADER + i * block, part);

  if (flat_len < 0x30 || memcmp(flat, "T2TD", 4) != 0) goto done;
  entries = be32(flat + 0x08);
  if (entries > 64 || 0x30 + (size_t)entries * ENTRY > flat_len) goto done;

  for (i = 0; i < entries; i++) {
    const uint8_t *e = flat + 0x30 + i * ENTRY;
    uint32_t type = be32(e), size = be32(e + 4), num = be32(e + 12), k;
    uint64_t off = be64(e + 16);

    if (type != TYPE_STATE) continue;
    if (size < 0x18 || num > T2PD_MAX) goto done;
    if (off > flat_len || (uint64_t)num * (size + REC_HEAD) > flat_len - off) goto done;
    for (k = 0; k < num; k++) {
      const uint8_t *r = flat + off + (size_t)k * (size + REC_HEAD);
      uint32_t id, flags;
      int64_t us;

      if (be32(r) != type || be32(r + 4) != size) goto done;
      r += REC_HEAD;
      id = be32(r);
      flags = be32(r + 4);
      if (id >= T2PD_MAX || !(flags & 1)) continue;
      us = (int64_t)be64(r + 0x10);
      out->earned[out->count].id = (uint16_t)id;
      out->earned[out->count].when = us > EPOCH_US ? (us - EPOCH_US) / 1000000 : 0;
      out->count++;
    }
    out->total = (int)num;
    rc = 0;
    break;
  }
done:
  free(flat);
  return rc;
}
