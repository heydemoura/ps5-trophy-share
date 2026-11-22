/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */

/* The few things that differ between the console and a PC (where the tests run). */
#ifndef TS_PLATFORM_H
#define TS_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

int plat_init(void);

/* Host name -> IPv4 address in network byte order. 0 on success. */
int plat_resolve(const char *host, uint32_t *ip);

void plat_random(void *buf, size_t len);

/* A message on the TV (and on stdout). */
void plat_notify(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* The console user who is on screen right now: local user ID and name. 0 on success. */
int plat_foreground_user(uint32_t *uid, char *name, size_t name_sz);

/* Prefix for every console path. Empty on the console; a folder that looks like a
 * console's disk on a PC. */
const char *plat_root(void);

#endif
