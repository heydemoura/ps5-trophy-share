/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */

/* PC stand-in for the console, for the tests:
 *   TS_ROOT  folder laid out like the console's disk (default: ./fakeroot)
 *   TS_USER  the "signed-in" local user as hex ID, optionally ":name" (default 10000001:Tester) */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "platform.h"

int plat_init(void) {
  return 0;
}

int plat_resolve(const char *host, uint32_t *ip) {
  struct addrinfo hints, *res;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return -1;
  *ip = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
  freeaddrinfo(res);
  return 0;
}

void plat_random(void *buf, size_t len) {
  FILE *f = fopen("/dev/urandom", "rb");
  if (!f || fread(buf, 1, len, f) != len) abort();
  fclose(f);
}

void plat_notify(const char *fmt, ...) {
  va_list ap;

  printf("[notify] ");
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
  fflush(stdout);
}

int plat_foreground_user(uint32_t *uid, char *name, size_t name_sz) {
  const char *u = getenv("TS_USER");
  const char *colon;

  if (!u) u = "10000001:Tester";
  *uid = (uint32_t)strtoul(u, NULL, 16);
  colon = strchr(u, ':');
  if (name_sz) snprintf(name, name_sz, "%s", colon ? colon + 1 : "");
  return *uid ? 0 : -1;
}

const char *plat_root(void) {
  const char *r = getenv("TS_ROOT");
  return r ? r : "fakeroot";
}
