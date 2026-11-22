/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */

/* A small HTTP/1.1 client: one request per connection, the whole answer in memory.
 *
 * TLS is BearSSL with our own list of root certificates (trust_anchors.h) instead of
 * the console's. A console that runs payloads stays on old firmware, so its certificate
 * store never gets newer roots; ours is updated with the payload. The same code runs on
 * a PC, which is how the tests exercise it. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "bearssl.h"
#include "http.h"
#include "platform.h"
#include "trust_anchors.h"

#define MAX_BODY (4u * 1024u * 1024u)
#define CONNECT_MS 15000
#define IO_SECONDS 40

/* Seconds since 1970 when this file was built: a console with an unset clock would
 * otherwise reject every certificate as "not valid yet". */
#ifndef BUILD_TIME
#define BUILD_TIME 1795305600L
#endif

static char g_agent[64] = "ps5-trophy-share";

int http_init(const char *user_agent) {
  snprintf(g_agent, sizeof(g_agent), "%s", user_agent);
  return 0;
}

void http_fini(void) {}

void http_free(http_res_t *res) {
  free(res->body);
  res->body = NULL;
  res->len = 0;
}

/* ── sockets ─────────────────────────────────────────────────────────────── */

static int tcp_connect(const char *host, int port) {
  struct sockaddr_in sa;
  struct timeval tv = {IO_SECONDS, 0};
  struct pollfd pfd;
  uint32_t ip;
  int fd, fl, err = 0;
  socklen_t len = sizeof(err);

  if (plat_resolve(host, &ip) != 0) return -HTTP_E_DNS;
  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -HTTP_E_CONNECT;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons((uint16_t)port);
  sa.sin_addr.s_addr = ip;

  /* Non-blocking connect so a dead route fails in seconds, not minutes. */
  fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
    if (errno != EINPROGRESS) goto fail;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    if (poll(&pfd, 1, CONNECT_MS) <= 0) goto fail;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) goto fail;
  }
  fcntl(fd, F_SETFL, fl);
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  return fd;
fail:
  close(fd);
  return -HTTP_E_CONNECT;
}

static int sock_read(void *ctx, unsigned char *buf, size_t len) {
  for (;;) {
    ssize_t n = read(*(int *)ctx, buf, len);
    if (n > 0) return (int)n;
    if (n < 0 && errno == EINTR) continue;
    return -1;
  }
}

static int sock_write(void *ctx, const unsigned char *buf, size_t len) {
  for (;;) {
    ssize_t n = write(*(int *)ctx, buf, len);
    if (n > 0) return (int)n;
    if (n < 0 && errno == EINTR) continue;
    return -1;
  }
}

/* ── one connection, plain or TLS ────────────────────────────────────────── */

typedef struct {
  int fd;
  int tls;
  br_ssl_client_context sc;
  br_x509_minimal_context xc;
  br_sslio_context io;
  unsigned char buf[BR_SSL_BUFSIZE_BIDI];
} conn_t;

static int conn_write(conn_t *c, const void *data, size_t len) {
  const unsigned char *p = data;
  if (c->tls) return br_sslio_write_all(&c->io, data, len);
  while (len) {
    int n = sock_write(&c->fd, p, len);
    if (n <= 0) return -1;
    p += n;
    len -= (size_t)n;
  }
  return 0;
}

static int conn_read(conn_t *c, void *data, size_t len) {
  if (c->tls) return br_sslio_read(&c->io, data, len);
  return sock_read(&c->fd, data, len);
}

static int conn_open(conn_t *c, const char *host, int port, int tls) {
  unsigned char seed[32];
  time_t now = time(NULL);

  c->tls = tls;
  if ((c->fd = tcp_connect(host, port)) < 0) return c->fd;
  if (!tls) return 0;

  br_ssl_client_init_full(&c->sc, &c->xc, TAs, TAs_NUM);
  if (now < BUILD_TIME) now = BUILD_TIME;
  br_x509_minimal_set_time(&c->xc, (uint32_t)(now / 86400) + 719528, (uint32_t)(now % 86400));
  plat_random(seed, sizeof(seed));
  br_ssl_engine_inject_entropy(&c->sc.eng, seed, sizeof(seed));
  br_ssl_engine_set_buffer(&c->sc.eng, c->buf, sizeof(c->buf), 1);
  br_ssl_client_reset(&c->sc, host, 0);
  br_sslio_init(&c->io, &c->sc.eng, sock_read, &c->fd, sock_write, &c->fd);
  return 0;
}

/* ── the request ─────────────────────────────────────────────────────────── */

/* "https://host[:port]/path" -> parts. Returns 0 on success. */
static int split_url(const char *url, int *tls, char *host, size_t host_sz, int *port, const char **path) {
  const char *p, *slash, *colon;
  size_t n;

  if (!strncmp(url, "https://", 8)) { *tls = 1; p = url + 8; *port = 443; }
  else if (!strncmp(url, "http://", 7)) { *tls = 0; p = url + 7; *port = 80; }
  else return -1;
  slash = strchr(p, '/');
  n = slash ? (size_t)(slash - p) : strlen(p);
  colon = memchr(p, ':', n);
  if (colon) {
    *port = atoi(colon + 1);
    n = (size_t)(colon - p);
  }
  if (!n || n >= host_sz || *port <= 0 || *port > 65535) return -1;
  memcpy(host, p, n);
  host[n] = 0;
  *path = slash ? slash : "/";
  return 0;
}

/* Value of a header in the block [head, end), or NULL. Case-insensitive name. */
static const char *header(const char *head, const char *end, const char *name) {
  size_t n = strlen(name);
  const char *p = head;
  while (p < end) {
    const char *eol = memchr(p, '\n', (size_t)(end - p));
    if (!eol) break;
    if ((size_t)(eol - p) > n && !strncasecmp(p, name, n) && p[n] == ':') {
      p += n + 1;
      while (p < eol && (*p == ' ' || *p == '\t')) p++;
      return p;
    }
    p = eol + 1;
  }
  return NULL;
}

/* Rewrites a chunked body in place. Returns the new length, or -1 if it is malformed. */
static long dechunk(char *body, size_t len) {
  size_t in = 0, out = 0;
  for (;;) {
    char *end;
    unsigned long n;
    if (in >= len) return -1;
    n = strtoul(body + in, &end, 16);
    if (end == body + in) return -1;
    while (end < body + len && *end != '\n') end++; /* skip chunk extensions */
    in = (size_t)(end - body) + 1;
    if (n == 0) return (long)out;
    if (n > len || in + n > len) return -1;
    memmove(body + out, body + in, n);
    out += n;
    in += n;
    if (in + 2 <= len && body[in] == '\r') in += 2;
    else if (in + 1 <= len && body[in] == '\n') in += 1;
  }
}

int http_request(const char *method, const char *url, const char *bearer,
                 const char *ctype, const void *body, size_t body_len,
                 http_res_t *res) {
  char host[128], head[1024];
  const char *path, *v;
  char *hend;
  conn_t *c;
  size_t cap = 8192, len = 0;
  char *buf;
  int tls, port, n, rc;

  memset(res, 0, sizeof(*res));
  if (split_url(url, &tls, host, sizeof(host), &port, &path) != 0) {
    res->status = HTTP_E_URL;
    return -1;
  }
  if (!(c = malloc(sizeof(*c)))) return -1;
  if ((rc = conn_open(c, host, port, tls)) != 0) {
    res->status = -rc;
    free(c);
    return -1;
  }

  n = snprintf(head, sizeof(head),
               "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nAccept: application/json\r\n"
               "Connection: close\r\nContent-Length: %zu\r\n",
               method, path, host, g_agent, body ? body_len : 0);
  if (bearer && n < (int)sizeof(head))
    n += snprintf(head + n, sizeof(head) - (size_t)n, "Authorization: Bearer %s\r\n", bearer);
  if (ctype && n < (int)sizeof(head))
    n += snprintf(head + n, sizeof(head) - (size_t)n, "Content-Type: %s\r\n", ctype);
  if (n < (int)sizeof(head)) n += snprintf(head + n, sizeof(head) - (size_t)n, "\r\n");
  if (n >= (int)sizeof(head)) { res->status = HTTP_E_URL; goto fail; }

  if (conn_write(c, head, (size_t)n) != 0 || (body && body_len && conn_write(c, body, body_len) != 0) ||
      (c->tls && br_sslio_flush(&c->io) != 0)) {
    /* With TLS the first write runs the handshake: say why it failed (certificate, clock...). */
    res->status = c->tls && br_ssl_engine_last_error(&c->sc.eng) ? HTTP_E_TLS + br_ssl_engine_last_error(&c->sc.eng) : HTTP_E_SEND;
    goto fail;
  }

  if (!(buf = malloc(cap))) goto fail;
  for (;;) {
    if (len + 1 >= cap) {
      char *p;
      if (cap >= MAX_BODY || !(p = realloc(buf, cap * 2))) break;
      buf = p;
      cap *= 2;
    }
    n = conn_read(c, buf + len, cap - len - 1);
    if (n <= 0) break;
    len += (size_t)n;
  }
  buf[len] = 0;

  if (len < 12 || strncmp(buf, "HTTP/1.", 7) != 0 || !(hend = strstr(buf, "\r\n\r\n"))) {
    /* Nothing usable came back. With TLS, say why (bad certificate, wrong clock...). */
    res->status = c->tls && br_ssl_engine_last_error(&c->sc.eng) ? HTTP_E_TLS + br_ssl_engine_last_error(&c->sc.eng) : HTTP_E_RECV;
    free(buf);
    goto fail;
  }
  res->status = atoi(buf + 9);
  v = header(buf, hend + 2, "Transfer-Encoding");
  hend += 4;
  len -= (size_t)(hend - buf);
  if (v && !strncasecmp(v, "chunked", 7)) {
    long m = dechunk(hend, len);
    if (m < 0) { res->status = HTTP_E_RECV; free(buf); goto fail; }
    len = (size_t)m;
  }
  memmove(buf, hend, len);
  buf[len] = 0;
  res->body = buf;
  res->len = len;

  if (c->tls) br_sslio_close(&c->io);
  close(c->fd);
  free(c);
  return 0;

fail:
  close(c->fd);
  free(c);
  return -1;
}
