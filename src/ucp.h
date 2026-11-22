/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */

/* TROPHY.UCP: the archive with a trophy set's definitions (JSON) and pictures (PNG).
 * See docs/FORMATS.md for the layout. */
#ifndef TS_UCP_H
#define TS_UCP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  char name[33];
  uint64_t off;
  uint64_t size;
} ucp_entry_t;

/* Reads the list of files. `*entries` is malloc'd. 0 on success. */
int ucp_list(const char *path, ucp_entry_t **entries, int *count);

/* The entry called `name`, or NULL. */
const ucp_entry_t *ucp_find(const ucp_entry_t *entries, int count, const char *name);

/* Reads one file from the archive into a malloc'd, NUL-terminated buffer.
 * Fails if it is larger than `max`. 0 on success. */
int ucp_read(const char *path, const ucp_entry_t *e, size_t max, char **buf, size_t *len);

#endif
