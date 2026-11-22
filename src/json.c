/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

static const char *ws(const char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  return p;
}

/* End of the string that starts at `p` (the opening quote): one past the closing quote. */
static const char *skip_string(const char *p) {
  for (p++; *p; p++) {
    if (*p == '\\') {
      if (!p[1]) return NULL;
      p++;
    } else if (*p == '"') {
      return p + 1;
    }
  }
  return NULL;
}

/* One past the end of the value that starts at `p`, or NULL if the text is broken. */
static const char *skip_value(const char *p) {
  int depth = 0;

  p = ws(p);
  if (*p == '"') return skip_string(p);
  if (*p != '{' && *p != '[') {
    const char *s = p;
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
    return p > s ? p : NULL;
  }
  for (; *p; p++) {
    if (*p == '"') {
      if (!(p = skip_string(p))) return NULL;
      p--;
    } else if (*p == '{' || *p == '[') {
      depth++;
    } else if (*p == '}' || *p == ']') {
      if (--depth == 0) return p + 1;
    }
  }
  return NULL;
}

const char *json_find(const char *json, const char *path) {
  const char *p = json;

  while (p && *path) {
    const char *dot = strchr(path, '.');
    size_t klen = dot ? (size_t)(dot - path) : strlen(path);
    const char *found = NULL;

    p = ws(p);
    if (*p != '{') return NULL;
    p = ws(p + 1);
    while (*p == '"') {
      const char *kend = skip_string(p);
      const char *val;
      if (!kend) return NULL;
      val = ws(kend);
      if (*val != ':') return NULL;
      val = ws(val + 1);
      if ((size_t)(kend - p - 2) == klen && !memcmp(p + 1, path, klen)) {
        found = val;
        break;
      }
      if (!(p = skip_value(val))) return NULL;
      p = ws(p);
      if (*p != ',') break;
      p = ws(p + 1);
    }
    if (!found) return NULL;
    p = found;
    path += klen + (dot ? 1 : 0);
  }
  return p;
}

static int hex4(const char *p) {
  int v = 0;
  for (int i = 0; i < 4; i++) {
    char c = p[i];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= c - '0';
    else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
    else return -1;
  }
  return v;
}

int json_copy(const char *value, char *out, size_t out_sz) {
  const char *p = value;
  size_t n = 0;

  if (!p || *p != '"' || !out_sz) return -1;
  for (p++; *p && *p != '"'; p++) {
    unsigned c = (unsigned char)*p;
    if (c == '\\') {
      p++;
      switch (*p) {
        case 'n': c = '\n'; break;
        case 't': c = '\t'; break;
        case 'r': c = '\r'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'u': {
          int u = hex4(p + 1);
          if (u < 0) return -1;
          p += 4;
          /* UTF-8, basic plane only: enough for names shown in a notification. */
          if (u < 0x80) { c = (unsigned)u; break; }
          if (n + 4 >= out_sz) goto done;
          if (u < 0x800) {
            out[n++] = (char)(0xc0 | (u >> 6));
          } else {
            out[n++] = (char)(0xe0 | (u >> 12));
            out[n++] = (char)(0x80 | ((u >> 6) & 0x3f));
          }
          c = 0x80 | ((unsigned)u & 0x3f);
          break;
        }
        case 0: return -1;
        default: c = (unsigned char)*p; break;
      }
    }
    if (n + 1 >= out_sz) break;
    out[n++] = (char)c;
  }
done:
  out[n] = 0;
  return 0;
}

int json_str(const char *json, const char *path, char *out, size_t out_sz) {
  if (out_sz) out[0] = 0;
  return json_copy(json_find(json, path), out, out_sz);
}

int json_long(const char *json, const char *path, long *out) {
  const char *p = json_find(json, path);
  char *end;
  long v;

  if (!p || (*p != '-' && (*p < '0' || *p > '9'))) return -1;
  v = strtol(p, &end, 10);
  if (end == p) return -1;
  *out = v;
  return 0;
}

const char *json_first(const char *array) {
  const char *p;
  if (!array || *array != '[') return NULL;
  p = ws(array + 1);
  return *p == ']' || !*p ? NULL : p;
}

const char *json_next(const char *element) {
  const char *p = skip_value(element);
  if (!p) return NULL;
  p = ws(p);
  if (*p != ',') return NULL;
  return ws(p + 1);
}

size_t json_quote(char *out, size_t out_sz, const char *in) {
  size_t n = 0;

  if (out_sz < 3) return 0;
  out[n++] = '"';
  for (; *in && n + 8 < out_sz; in++) {
    unsigned char c = (unsigned char)*in;
    if (c == '"' || c == '\\') {
      out[n++] = '\\';
      out[n++] = (char)c;
    } else if (c < 0x20) {
      n += (size_t)snprintf(out + n, out_sz - n, "\\u%04x", c);
    } else {
      out[n++] = (char)c;
    }
  }
  out[n++] = '"';
  out[n] = 0;
  return n;
}
