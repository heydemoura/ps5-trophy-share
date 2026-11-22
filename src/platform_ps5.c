/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "platform.h"

typedef struct { char unused[45]; char message[3075]; } notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

int sceUserServiceInitialize(void *);
int sceUserServiceGetForegroundUser(int *);
int sceUserServiceGetUserName(int, char *, size_t);

int sceNetInit(void);
int sceNetPoolCreate(const char *, int, int);
int sceNetResolverCreate(const char *, int, int);
int sceNetResolverStartNtoa(int, const char *, struct in_addr *, int, int, int);
int sceNetResolverDestroy(int);

static int g_pool = -1;

int plat_init(void) {
  sceNetInit();
  g_pool = sceNetPoolCreate("trophyshare", 16 * 1024, 0);
  sceUserServiceInitialize(NULL);
  return 0;
}

int plat_resolve(const char *host, uint32_t *ip) {
  struct in_addr a;
  int rid, rc;

  if (inet_pton(AF_INET, host, &a) == 1) {
    *ip = a.s_addr;
    return 0;
  }
  /* The console has no /etc/resolv.conf: names go through its own resolver. */
  if ((rid = sceNetResolverCreate("trophyshare", g_pool, 0)) < 0) return -1;
  rc = sceNetResolverStartNtoa(rid, host, &a, 10 * 1000 * 1000, 2, 0);
  sceNetResolverDestroy(rid);
  if (rc < 0) return -1;
  *ip = a.s_addr;
  return 0;
}

void plat_random(void *buf, size_t len) {
  arc4random_buf(buf, len);
}

void plat_notify(const char *fmt, ...) {
  notify_request_t req;
  va_list ap;

  memset(&req, 0, sizeof(req));
  va_start(ap, fmt);
  vsnprintf(req.message, sizeof(req.message), fmt, ap);
  va_end(ap);
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
  printf("[notify] %s\n", req.message);
  fflush(stdout);
}

int plat_foreground_user(uint32_t *uid, char *name, size_t name_sz) {
  int id = 0;

  if (sceUserServiceGetForegroundUser(&id) != 0 || id <= 0) return -1;
  *uid = (uint32_t)id;
  if (name_sz) {
    name[0] = 0;
    if (sceUserServiceGetUserName(id, name, name_sz) != 0) name[0] = 0;
    name[name_sz - 1] = 0;
  }
  return 0;
}

const char *plat_root(void) {
  return "";
}
