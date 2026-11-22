/* ps5-trophy-share - share PS5 trophies on a public profile page.
 * Copyright (C) 2026 Heyde Moura
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 * See the LICENSE file. */

/* What the payload does, in order:
 *   1. find the console's users and the trophy sets each one has;
 *   2. for a user without a key, show a code and wait for it to be typed on the site;
 *   3. send each linked user's earned trophies, plus the definitions of any trophy set
 *      the server has not seen yet;
 *   4. sleep, and do 3 again whenever a trophy file changes. */
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "app.h"
#include "http.h"
#include "icon.h"
#include "json.h"
#include "platform.h"
#include "t2pd.h"
#include "ucp.h"

#define HOME "/user/home"
#define CONF "/user/trophy2/nobackup/conf"
#define DATA "/data/trophyshare"
#define MAX_USERS 16
#define MAX_SETS 3000
#define CHECK_SECONDS 20
#define RESYNC_SECONDS (6 * 3600)
#define LOG_MAX (256 * 1024)

typedef struct {
  uint32_t uid;
  uint64_t sig;        /* fingerprint of the trophy files at the last good sync */
  time_t synced;
  int asked;           /* a link code was already shown in this run */
  int announced;       /* the first sync was already reported on screen */
} user_t;

#define MAX_ICON_SKIP 512

static char g_api[200];
static char g_icon_skip[MAX_ICON_SKIP][16]; /* sets that turned out to have no usable picture */
static int g_icon_nskip;
static user_t g_users[MAX_USERS];
static int g_nusers;
static int g_offline_told;

/* ── small tools ─────────────────────────────────────────────────────────── */

static void path(char *out, size_t sz, const char *fmt, ...) {
  va_list ap;
  int n = snprintf(out, sz, "%s", plat_root());
  va_start(ap, fmt);
  vsnprintf(out + n, sz - (size_t)n, fmt, ap);
  va_end(ap);
}

void app_log(const char *fmt, ...) {
  char line[600], file[300], stamp[32];
  time_t now = time(NULL);
  struct tm tm;
  struct stat st;
  va_list ap;
  FILE *f;

  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  printf("[trophyshare] %s\n", line);
  fflush(stdout);

  path(file, sizeof(file), DATA "/log.txt");
  if (stat(file, &st) == 0 && st.st_size > LOG_MAX) unlink(file);
  if (!(f = fopen(file, "a"))) return;
  gmtime_r(&now, &tm);
  strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);
  fprintf(f, "%s %s\n", stamp, line);
  fclose(f);
}

typedef struct {
  char *p;
  size_t len, cap;
  int bad;
} sb_t;

static void sb_raw(sb_t *sb, const char *data, size_t n) {
  if (sb->bad) return;
  if (sb->len + n + 1 > sb->cap) {
    size_t cap = sb->cap ? sb->cap * 2 : 4096;
    char *p;
    while (cap < sb->len + n + 1) cap *= 2;
    if (!(p = realloc(sb->p, cap))) { sb->bad = 1; return; }
    sb->p = p;
    sb->cap = cap;
  }
  memcpy(sb->p + sb->len, data, n);
  sb->len += n;
  sb->p[sb->len] = 0;
}

static void sb_add(sb_t *sb, const char *fmt, ...) {
  char tmp[512];
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n > 0) sb_raw(sb, tmp, (size_t)n < sizeof(tmp) ? (size_t)n : sizeof(tmp) - 1);
}

static int read_file(const char *file, size_t max, char **buf, size_t *len) {
  struct stat st;
  FILE *f;
  char *p;

  *buf = NULL;
  if (stat(file, &st) != 0 || st.st_size <= 0 || (size_t)st.st_size > max) return -1;
  if (!(f = fopen(file, "rb"))) return -1;
  if (!(p = malloc((size_t)st.st_size + 1))) { fclose(f); return -1; }
  *len = fread(p, 1, (size_t)st.st_size, f);
  fclose(f);
  p[*len] = 0;
  *buf = p;
  return 0;
}

/* First line of a small text file, without spaces around it. 0 on success. */
static int read_line(const char *file, char *out, size_t sz) {
  char *buf, *s, *e;
  size_t len;

  out[0] = 0;
  if (read_file(file, 4096, &buf, &len) != 0) return -1;
  s = buf;
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
  e = s;
  while (*e && *e != '\r' && *e != '\n' && *e != ' ' && *e != '\t') e++;
  *e = 0;
  snprintf(out, sz, "%s", s);
  free(buf);
  return out[0] ? 0 : -1;
}

static int write_line(const char *file, const char *text) {
  FILE *f = fopen(file, "w");
  if (!f) return -1;
  fprintf(f, "%s\n", text);
  fclose(f);
  chmod(file, 0600);
  return 0;
}

static int is_np(const char *s) {
  /* NPWR12345_00 */
  if (strlen(s) != 12 || strncmp(s, "NPWR", 4) != 0 || s[9] != '_') return 0;
  for (int i = 4; i < 12; i++)
    if (i != 9 && (s[i] < '0' || s[i] > '9')) return 0;
  return 1;
}

static user_t *user_slot(uint32_t uid) {
  for (int i = 0; i < g_nusers; i++)
    if (g_users[i].uid == uid) return &g_users[i];
  if (g_nusers == MAX_USERS) return NULL;
  memset(&g_users[g_nusers], 0, sizeof(user_t));
  g_users[g_nusers].uid = uid;
  return &g_users[g_nusers++];
}

/* Waits up to `seconds`. Returns 1 if a newer copy of the payload asked us to quit. */
static int wait_or_quit(int ctrl_fd, int seconds) {
  struct pollfd pfd = {ctrl_fd, POLLIN, 0};
  char buf[16];
  int fd;

  if (ctrl_fd < 0) {
    sleep((unsigned)seconds);
    return 0;
  }
  if (poll(&pfd, 1, seconds * 1000) <= 0) return 0;
  if ((fd = accept(ctrl_fd, NULL, NULL)) < 0) return 0;
  memset(buf, 0, sizeof(buf));
  if (read(fd, buf, sizeof(buf) - 1) < 0) buf[0] = 0;
  close(fd);
  return !strncmp(buf, "quit", 4);
}

/* ── the server ──────────────────────────────────────────────────────────── */

/* One JSON call. Returns the HTTP status (body in *out, to be freed) or -1. */
static int call(const char *method, const char *route, const char *key, const char *body, size_t len, char **out) {
  char url[400];
  http_res_t res;

  if (out) *out = NULL;
  snprintf(url, sizeof(url), "%s%s", g_api, route);
  if (http_request(method, url, key, body ? "application/json" : NULL, body, len, &res) != 0) {
    app_log("%s %s: no answer (error %d)", method, route, res.status);
    return -1;
  }
  if (res.status != 200) {
    char msg[160] = "";
    json_str(res.body, "message", msg, sizeof(msg));
    app_log("%s %s: HTTP %d %s", method, route, res.status, msg);
  }
  if (out) *out = res.body;
  else http_free(&res);
  return res.status;
}

/* ── reading the console's trophy files ──────────────────────────────────── */

typedef struct {
  char np[16];
} setref_t;

/* The user's trophy sets, and a fingerprint that changes when any of them changes. */
static int list_sets(uint32_t uid, setref_t **out, int *count, uint64_t *sig) {
  char dir[300], file[600];
  struct dirent *e;
  struct stat st;
  setref_t *list = NULL;
  uint64_t h = 1469598103934665603ULL;
  int n = 0, cap = 0;
  DIR *d;

  *out = NULL;
  *count = 0;
  *sig = 0;
  path(dir, sizeof(dir), HOME "/%08x/trophy2/nobackup/data", uid);
  if (!(d = opendir(dir))) return 0; /* no PS5 trophies yet: not an error */
  while ((e = readdir(d)) && n < MAX_SETS) {
    if (!is_np(e->d_name)) continue;
    snprintf(file, sizeof(file), "%s/%s/TRPTITLE.DAT", dir, e->d_name);
    if (stat(file, &st) != 0) continue;
    if (n == cap) {
      setref_t *p = realloc(list, (size_t)(cap = cap ? cap * 2 : 32) * sizeof(*list));
      if (!p) break;
      list = p;
    }
    memcpy(list[n].np, e->d_name, 13);
    n++;
    /* Order of readdir() must not matter: add up one hash per file. */
    {
      uint64_t one = 1469598103934665603ULL;
      const char *c;
      for (c = e->d_name; *c; c++) one = (one ^ (uint8_t)*c) * 1099511628211ULL;
      one = (one ^ (uint64_t)st.st_mtime) * 1099511628211ULL;
      one = (one ^ (uint64_t)st.st_size) * 1099511628211ULL;
      h += one;
    }
  }
  closedir(d);
  *out = list;
  *count = n;
  *sig = h + (uint64_t)n;
  return 0;
}

static void ucp_path(char *out, size_t sz, const char *np) {
  path(out, sz, CONF "/%s/TROPHY.UCP", np);
}

/* tropconf.json of a set, malloc'd. */
static char *read_conf(const char *np, ucp_entry_t **entries, int *count) {
  char file[400], *conf = NULL;
  size_t len;

  ucp_path(file, sizeof(file), np);
  if (ucp_list(file, entries, count) != 0) return NULL;
  if (ucp_read(file, ucp_find(*entries, *count, "tropconf.json"), 1024 * 1024, &conf, &len) != 0) {
    free(*entries);
    *entries = NULL;
    return NULL;
  }
  return conf;
}

/* Appends {"np":..,"rev":..,"earned":[[id,time],..]} for one set. 0 on success. */
static int add_set(sb_t *sb, uint32_t uid, const char *np, int first) {
  char file[400], *data, *conf;
  ucp_entry_t *entries = NULL;
  size_t len;
  long rev = 0;
  t2pd_t *st;
  int n = 0;

  path(file, sizeof(file), HOME "/%08x/trophy2/nobackup/data/%s/TRPTITLE.DAT", uid, np);
  if (read_file(file, 4 * 1024 * 1024, &data, &len) != 0) return -1;
  if (!(st = malloc(sizeof(*st)))) { free(data); return -1; }
  if (t2pd_parse((const uint8_t *)data, len, st) != 0) {
    app_log("%s: cannot read TRPTITLE.DAT (%zu bytes)", np, len);
    free(data);
    free(st);
    return -1;
  }
  free(data);
  if ((conf = read_conf(np, &entries, &n))) {
    json_long(conf, "trophyDefinitionRevision", &rev);
    free(conf);
    free(entries);
  }
  sb_add(sb, "%s{\"np\":\"%s\",\"rev\":%ld,\"earned\":[", first ? "" : ",", np, rev);
  for (int i = 0; i < st->count; i++)
    sb_add(sb, "%s[%u,%lld]", i ? "," : "", st->earned[i].id, (long long)st->earned[i].when);
  sb_add(sb, "]}");
  free(st);
  return 0;
}

/* Sends tropconf.json and one tropmeta_xx.json of a set, as they are. */
static int upload_defs(const char *key, const char *np) {
  char file[400], route[64], lang[16] = "en-US", name[40], *conf, *meta = NULL;
  ucp_entry_t *entries = NULL;
  const ucp_entry_t *e;
  size_t len;
  sb_t sb = {0};
  int n = 0, rc;

  if (!(conf = read_conf(np, &entries, &n))) {
    app_log("%s: no TROPHY.UCP for it", np);
    return -1;
  }
  ucp_path(file, sizeof(file), np);
  /* English when there is one, else the set's own default language, else any. */
  if (!(e = ucp_find(entries, n, "tropmeta_en-US.json")) && !(e = ucp_find(entries, n, "tropmeta_en-GB.json"))) {
    if (json_str(conf, "defaultLanguage", lang, sizeof(lang)) == 0) {
      snprintf(name, sizeof(name), "tropmeta_%s.json", lang);
      e = ucp_find(entries, n, name);
    }
    for (int i = 0; !e && i < n; i++)
      if (!strncmp(entries[i].name, "tropmeta_", 9)) e = &entries[i];
  }
  if (e) sscanf(e->name, "tropmeta_%15[^.]", lang);
  if (ucp_read(file, e, 1024 * 1024, &meta, &len) != 0) {
    app_log("%s: no trophy names in TROPHY.UCP", np);
    free(conf);
    free(entries);
    return -1;
  }
  sb_add(&sb, "{\"lang\":\"%s\",\"conf\":", lang);
  sb_raw(&sb, conf, strlen(conf));
  sb_add(&sb, ",\"meta\":");
  sb_raw(&sb, meta, strlen(meta));
  sb_add(&sb, "}");
  free(conf);
  free(meta);
  free(entries);
  if (sb.bad) { free(sb.p); return -1; }
  snprintf(route, sizeof(route), "/v1/games/%s", np);
  rc = call("PUT", route, key, sb.p, sb.len, NULL);
  free(sb.p);
  return rc == 200 ? 0 : -1;
}

static void icon_skip(const char *np) {
  if (g_icon_nskip < MAX_ICON_SKIP) memcpy(g_icon_skip[g_icon_nskip++], np, 13);
}

/* Sends the game's picture, shrunk to a small JPEG. Asked for once per game, by the server. */
static int upload_icon(const char *key, const char *np) {
  char file[400], url[400], lang[16] = "", name[40], *conf, *png = NULL;
  ucp_entry_t *entries = NULL;
  const ucp_entry_t *e;
  unsigned char *jpg = NULL;
  size_t len = 0, jpg_len = 0;
  http_res_t res;
  int n = 0, rc = -1;

  /* A set with no usable picture stays that way: do not look again on every sync. */
  for (int i = 0; i < g_icon_nskip; i++)
    if (!strcmp(g_icon_skip[i], np)) return -1;

  if (!(conf = read_conf(np, &entries, &n))) return -1;
  ucp_path(file, sizeof(file), np);
  /* icon0_<language>.png: the English one, else the set's default language, else any. */
  if (!(e = ucp_find(entries, n, "icon0_en-US.png")) && !(e = ucp_find(entries, n, "icon0.png"))) {
    if (json_str(conf, "defaultLanguage", lang, sizeof(lang)) == 0) {
      snprintf(name, sizeof(name), "icon0_%s.png", lang);
      e = ucp_find(entries, n, name);
    }
    for (int i = 0; !e && i < n; i++)
      if (!strncmp(entries[i].name, "icon0", 5)) e = &entries[i];
  }
  free(conf);
  if (ucp_read(file, e, 8 * 1024 * 1024, &png, &len) != 0) {
    app_log("%s: no picture in TROPHY.UCP", np);
    free(entries);
    icon_skip(np);
    return -1;
  }
  free(entries);
  if (icon_make((const unsigned char *)png, len, ICON_SIDE, &jpg, &jpg_len) != 0) {
    app_log("%s: cannot read the picture (%zu bytes)", np, len);
    free(png);
    icon_skip(np);
    return -1;
  }
  free(png);
  snprintf(url, sizeof(url), "%s/v1/games/%s/icon", g_api, np);
  if (http_request("PUT", url, key, "image/jpeg", jpg, jpg_len, &res) == 0) {
    rc = res.status == 200 ? 0 : -1;
    if (rc) app_log("PUT /v1/games/%s/icon: HTTP %d", np, res.status);
    http_free(&res);
  } else {
    app_log("PUT /v1/games/%s/icon: no answer (error %d)", np, res.status);
  }
  if (rc == 0) app_log("%s: sent the picture (%zu bytes)", np, jpg_len);
  free(jpg);
  return rc;
}

/* The server lists, in "need_icon", the games it has no picture for yet. */
static void send_icons(const char *key, const char *resp) {
  char np[16];
  const char *el;

  if (!resp) return;
  for (el = json_first(json_find(resp, "need_icon")); el; el = json_next(el))
    if (json_copy(el, np, sizeof(np)) == 0 && is_np(np)) upload_icon(key, np);
}

/* ── sync ────────────────────────────────────────────────────────────────── */

enum { SYNC_OK = 0, SYNC_RETRY = -1, SYNC_UNLINKED = -2 };

typedef struct {
  long stored, fresh, trophies, games;
  char nick[80], url[160];
} sync_info_t;

/* Sends the sets in `sets` (all of them, or only those listed in the JSON array
 * `only`). The server's answer is returned in *resp. */
static int post_sets(uint32_t uid, const char *key, const setref_t *sets, int n, const char *only, char **resp) {
  sb_t sb = {0};
  int sent = 0, status;

  sb_add(&sb, "{\"v\":1,\"sets\":[");
  for (int i = 0; i < n; i++) {
    if (only) {
      char want[20];
      snprintf(want, sizeof(want), "\"%s\"", sets[i].np);
      if (!strstr(only, want)) continue;
    }
    if (add_set(&sb, uid, sets[i].np, sent == 0) == 0) sent++;
  }
  sb_add(&sb, "]}");
  if (sb.bad) { free(sb.p); return SYNC_RETRY; }
  status = call("POST", "/v1/sync", key, sb.p, sb.len, resp);
  free(sb.p);
  if (status == 401) return SYNC_UNLINKED;
  return status == 200 ? sent : SYNC_RETRY;
}

static void read_info(const char *resp, sync_info_t *info) {
  long v = 0, p = 0, g = 0, s = 0, b = 0;
  if (json_long(resp, "stored", &v) == 0) info->stored += v;
  if (json_long(resp, "new_trophies", &v) == 0) info->fresh += v;
  json_long(resp, "totals.p", &p);
  json_long(resp, "totals.g", &g);
  json_long(resp, "totals.s", &s);
  json_long(resp, "totals.b", &b);
  json_long(resp, "totals.games", &info->games);
  info->trophies = p + g + s + b;
  json_str(resp, "player.nickname", info->nick, sizeof(info->nick));
  json_str(resp, "url", info->url, sizeof(info->url));
}

static int sync_user(uint32_t uid, const char *key, const setref_t *sets, int n, sync_info_t *info) {
  char *resp = NULL, *resp2 = NULL, np[16];
  const char *need, *el;
  int rc, missing = 0;

  memset(info, 0, sizeof(*info));
  if ((rc = post_sets(uid, key, sets, n, NULL, &resp)) < 0) { free(resp); return rc; }
  read_info(resp, info);

  /* Trophy sets the server has never seen: send what they are, then those sets again. */
  need = json_find(resp, "need");
  for (el = json_first(need); el; el = json_next(el)) {
    if (json_copy(el, np, sizeof(np)) != 0 || !is_np(np)) continue;
    missing++;
    if (upload_defs(key, np) == 0) app_log("%s: sent the trophy list", np);
  }
  if (missing) {
    rc = post_sets(uid, key, sets, n, need, &resp2);
    if (rc >= 0) read_info(resp2, info);
  }
  /* Pictures last: they are the least important part and the largest. */
  if (rc >= 0) {
    send_icons(key, resp);
    send_icons(key, resp2);
  }
  free(resp2);
  free(resp);
  return rc < 0 ? rc : SYNC_OK;
}

/* ── linking ─────────────────────────────────────────────────────────────── */

static void key_path(char *out, size_t sz, uint32_t uid) {
  path(out, sz, DATA "/keys/%08x", uid);
}

/* Shows a code on screen and waits until it is typed on the site (or it runs out).
 * Returns 0 with the key saved, 1 if we were asked to quit, <0 otherwise. */
static int link_user(uint32_t uid, const char *user_name, char *key, size_t key_sz, int ctrl_fd) {
  char body[256], label[96], quoted[200], code[16], secret[64], show[16], url[160], nick[80], file[300];
  char *resp = NULL;
  const char *where;
  long expires = 600, interval = 5;
  time_t start, shown = 0;
  int status, n;

  snprintf(label, sizeof(label), "PS5 - %s", user_name[0] ? user_name : "user");
  json_quote(quoted, sizeof(quoted), label);
  n = snprintf(body, sizeof(body), "{\"label\":%s}", quoted);
  if ((status = call("POST", "/v1/link/start", NULL, body, (size_t)n, &resp)) != 200) {
    free(resp);
    return -1;
  }
  json_str(resp, "code", code, sizeof(code));
  json_str(resp, "secret", secret, sizeof(secret));
  json_str(resp, "show", show, sizeof(show));
  json_str(resp, "url", url, sizeof(url));
  json_long(resp, "expires_in", &expires);
  json_long(resp, "interval", &interval);
  free(resp);
  if (!code[0] || !secret[0]) return -1;
  if (interval < 2) interval = 2;
  where = strstr(url, "://") ? strstr(url, "://") + 3 : url;

  n = snprintf(body, sizeof(body), "{\"code\":\"%s\",\"secret\":\"%s\"}", code, secret);
  for (start = time(NULL); time(NULL) - start < expires;) {
    if (time(NULL) - shown >= 60) {
      plat_notify("Trophy Share\nOn your phone or PC open %s\nand type the code %s", where, show);
      shown = time(NULL);
    }
    if (wait_or_quit(ctrl_fd, (int)interval)) return 1;
    status = call("POST", "/v1/link/poll", NULL, body, (size_t)n, &resp);
    if (status == 200 && resp && strstr(resp, "\"linked\"") && json_str(resp, "key", key, key_sz) == 0 && key[0]) {
      json_str(resp, "player.nickname", nick, sizeof(nick));
      free(resp);
      key_path(file, sizeof(file), uid);
      if (write_line(file, key) != 0) app_log("cannot save the key in %s: %s", file, strerror(errno));
      plat_notify("Trophy Share\nThis console is now linked to %s", nick[0] ? nick : "your page");
      return 0;
    }
    free(resp);
    resp = NULL;
    if (status == 404 || status == 410) break;
  }
  plat_notify("Trophy Share\nThe code ran out. Run the payload again for a new one.");
  return -1;
}

/* ── the loop ────────────────────────────────────────────────────────────── */

static int list_users(uint32_t *uids, int max) {
  char dir[300];
  struct dirent *e;
  int n = 0;
  DIR *d;

  path(dir, sizeof(dir), HOME);
  if (!(d = opendir(dir))) return 0;
  while ((e = readdir(d)) && n < max) {
    char *end;
    unsigned long v;
    if (strlen(e->d_name) != 8) continue;
    v = strtoul(e->d_name, &end, 16);
    if (*end || !v) continue;
    uids[n++] = (uint32_t)v;
  }
  closedir(d);
  return n;
}

/* One pass over every user. Returns 1 to quit, else 0; *failed counts users not sent. */
static int pass(int ctrl_fd, int *failed) {
  uint32_t uids[MAX_USERS], fg = 0;
  char fg_name[64] = "", file[300], manual[300], key[128];
  int n, have_fg;

  *failed = 0;
  have_fg = plat_foreground_user(&fg, fg_name, sizeof(fg_name)) == 0;

  /* A key made by hand on the site belongs to whoever is signed in right now. */
  path(manual, sizeof(manual), DATA "/key");
  if (have_fg && read_line(manual, key, sizeof(key)) == 0) {
    key_path(file, sizeof(file), fg);
    if (write_line(file, key) == 0) unlink(manual);
  }

  n = list_users(uids, MAX_USERS);
  for (int i = 0; i < n; i++) {
    user_t *u = user_slot(uids[i]);
    setref_t *sets = NULL;
    sync_info_t info;
    uint64_t sig;
    int nsets, rc;

    if (!u) continue;
    key_path(file, sizeof(file), u->uid);
    if (read_line(file, key, sizeof(key)) != 0) {
      if (!have_fg || u->uid != fg || u->asked) continue;
      u->asked = 1;
      rc = link_user(u->uid, fg_name, key, sizeof(key), ctrl_fd);
      if (rc == 1) return 1;
      if (rc != 0) { (*failed)++; continue; }
    }

    list_sets(u->uid, &sets, &nsets, &sig);
    if (u->synced && sig == u->sig && time(NULL) - u->synced < RESYNC_SECONDS) {
      free(sets);
      continue;
    }
    rc = sync_user(u->uid, key, sets, nsets, &info);
    free(sets);
    if (rc == SYNC_UNLINKED) {
      /* Removed on the site: forget the key and offer a new code. */
      app_log("user %08x: the key is no longer valid, removing it", u->uid);
      unlink(file);
      u->asked = u->synced = 0;
      u->announced = 0;
      (*failed)++;
      continue;
    }
    if (rc != SYNC_OK) {
      if (!g_offline_told && !u->synced) {
        plat_notify("Trophy Share\nCannot reach the server. It keeps trying in the background.");
        g_offline_told = 1;
      }
      (*failed)++;
      continue;
    }
    u->sig = sig;
    u->synced = time(NULL);
    app_log("user %08x: %d sets checked, %ld updated, %ld new trophies", u->uid, nsets, info.stored, info.fresh);
    if (!u->announced) {
      u->announced = 1;
      plat_notify("Trophy Share\n%s: %ld %s from %ld %s now on your page", info.nick[0] ? info.nick : "Done",
                  info.trophies, info.trophies == 1 ? "trophy" : "trophies", info.games, info.games == 1 ? "game" : "games");
    }
  }
  return 0;
}

int app_run(const char *api, int once, int ctrl_fd) {
  char dir[300], file[300], custom[200];
  int failed = 0;

  path(dir, sizeof(dir), "/data");
  mkdir(dir, 0777);
  path(dir, sizeof(dir), DATA);
  mkdir(dir, 0700);
  path(dir, sizeof(dir), DATA "/keys");
  mkdir(dir, 0700);

  /* DATA/api holds another server address: for people who run their own. */
  path(file, sizeof(file), DATA "/api");
  snprintf(g_api, sizeof(g_api), "%s", read_line(file, custom, sizeof(custom)) == 0 ? custom : api);
  while (g_api[0] && g_api[strlen(g_api) - 1] == '/') g_api[strlen(g_api) - 1] = 0;
  app_log("ps5-trophy-share " TS_VERSION ", server %s", g_api);

  for (int wait = CHECK_SECONDS;;) {
    if (pass(ctrl_fd, &failed)) break;
    if (once) return failed ? 1 : 0;
    /* While the server cannot be reached, try less and less often (up to 5 minutes). */
    wait = failed ? (wait * 2 > 300 ? 300 : wait * 2) : CHECK_SECONDS;
    if (wait_or_quit(ctrl_fd, wait)) break;
  }
  app_log("asked to stop (a newer copy, or the stop payload), bye");
  return 0;
}
