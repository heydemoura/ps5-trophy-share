/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "app.h"
#include "http.h"
#include "platform.h"

static int ctrl_socket(void) {
  struct sockaddr_in sa;
  int fd, one = 1;

  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(TS_CTRL_PORT);
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  for (int attempt = 0; attempt < 90; attempt++) { /* up to 45 s: the old copy may be mid-request */
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0 && listen(fd, 2) == 0) return fd;
    close(fd);
    /* Taken: an older copy is running. Ask it to leave and try again. */
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) >= 0) {
      if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0 && write(fd, "quit\n", 5) < 0) {}
      close(fd);
    }
    usleep(500 * 1000);
  }
  return -1;
}

int main(int argc, char **argv) {
  const char *api = getenv("TS_API");
  int once = 0, ctrl = -1;

  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], "--once")) once = 1;

  signal(SIGPIPE, SIG_IGN); /* the sender closing its socket must not kill us */
  plat_init();
  http_init("ps5-trophy-share/" TS_VERSION);
  if (!once) ctrl = ctrl_socket();
  return app_run(api && api[0] ? api : TS_API_URL, once, ctrl);
}
