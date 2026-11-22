/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */
#ifndef TS_HTTP_H
#define TS_HTTP_H

#include <stddef.h>

typedef struct {
  int status;  /* HTTP status code */
  char *body;  /* malloc'd, always NUL terminated */
  size_t len;
} http_res_t;

/* When the server could not be reached, `status` is one of these instead of an HTTP
 * status. HTTP_E_TLS + n is BearSSL error n (see bearssl_ssl.h): 62 = certificate not
 * trusted, 54 = certificate expired or the clock is wrong. */
#define HTTP_E_URL 1
#define HTTP_E_DNS 2
#define HTTP_E_CONNECT 3
#define HTTP_E_SEND 4
#define HTTP_E_RECV 5
#define HTTP_E_TLS 1000

/* Both return 0 on success. */
int http_init(const char *user_agent);
void http_fini(void);

/* One request, whole answer in memory. `bearer`, `ctype` and `body` may be NULL.
 * Returns 0 when the server answered (any status), <0 when it could not be reached;
 * in that case `res->status` holds one of the HTTP_E_ codes. */
int http_request(const char *method, const char *url, const char *bearer,
                 const char *ctype, const void *body, size_t body_len,
                 http_res_t *res);
void http_free(http_res_t *res);

#endif
