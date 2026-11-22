/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */

/* Game icons: the 512 x 512 PNG in TROPHY.UCP becomes a small JPEG before it is sent,
 * about 10 KB instead of about 450 KB. */
#ifndef TS_ICON_H
#define TS_ICON_H

#include <stddef.h>

#define ICON_SIDE 192

/* PNG in, JPEG out (malloc'd), no side longer than `max_side`. 0 on success. */
int icon_make(const unsigned char *png, size_t png_len, int max_side, unsigned char **jpg, size_t *jpg_len);

#endif
