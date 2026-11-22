/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */

/* ps5-trophy-share-stop.elf: tells a running ps5-trophy-share to quit, then exits. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "app.h"

int main(void) {
  struct sockaddr_in sa;
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(TS_CTRL_PORT);
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (fd < 0 || connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
    printf("ps5-trophy-share is not running\n");
    return 1;
  }
  if (write(fd, "quit\n", 5) != 5) return 1;
  close(fd);
  printf("asked ps5-trophy-share to stop\n");
  return 0;
}
