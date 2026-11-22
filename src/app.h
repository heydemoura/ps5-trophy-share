/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */
#ifndef TS_APP_H
#define TS_APP_H

/* Only one copy should run. A new copy tells the old one to quit through this local
 * port, then takes the port for itself. */
#define TS_CTRL_PORT 38275

/* Runs the payload: link the console if needed, send the trophies, then keep watching
 * for new ones. `api` is the server's base address. With `once` it makes a single pass
 * and returns (0 = every linked user was sent). `ctrl_fd` is the socket a newer copy of
 * the payload uses to ask this one to quit, or -1. */
int app_run(const char *api, int once, int ctrl_fd);

void app_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
