/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */

/* Just enough JSON to read the server's answers and to write strings safely. */
#ifndef TS_JSON_H
#define TS_JSON_H

#include <stddef.h>

/* The value at a dotted path such as "player.nickname" inside the object `json`,
 * or NULL. The result points into `json`, at the first character of the value. */
const char *json_find(const char *json, const char *path);

/* Copies the string at `path`, unescaped, into `out`. 0 on success. */
int json_str(const char *json, const char *path, char *out, size_t out_sz);

/* Reads the number at `path`. 0 on success. */
int json_long(const char *json, const char *path, long *out);

/* Walks an array: json_first(value at '[') gives the first element or NULL,
 * json_next(element) the one after it or NULL. */
const char *json_first(const char *array);
const char *json_next(const char *element);

/* Copies the string value at `value` (which points at its opening quote). 0 on success. */
int json_copy(const char *value, char *out, size_t out_sz);

/* Writes `in` as a JSON string, quotes included. Returns the length written. */
size_t json_quote(char *out, size_t out_sz, const char *in);

#endif
