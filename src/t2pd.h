/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */

/* TRPTITLE.DAT ("T2PD"): one user's progress in one PS5 trophy set.
 * See docs/FORMATS.md for the layout. */
#ifndef TS_T2PD_H
#define TS_T2PD_H

#include <stddef.h>
#include <stdint.h>

#define T2PD_MAX 512

typedef struct {
  int total;                  /* trophies in the set */
  int count;                  /* how many of them are earned */
  struct {
    uint16_t id;
    int64_t when;             /* unix seconds, UTC; 0 = unknown */
  } earned[T2PD_MAX];
} t2pd_t;

/* 0 on success, <0 when the data is not a trophy title file we understand. */
int t2pd_parse(const uint8_t *data, size_t len, t2pd_t *out);

#endif
