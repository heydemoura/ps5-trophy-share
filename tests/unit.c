/* Unit tests for the file readers and the JSON helpers.
 *   build/unit <fake console root made by tests/make_fixture.py> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "icon.h"
#include "json.h"
#include "t2pd.h"
#include "ucp.h"

static int g_fail;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_fail++; } } while (0)

static uint8_t *slurp(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  uint8_t *p;
  if (!f) { printf("cannot open %s\n", path); exit(2); }
  fseek(f, 0, SEEK_END);
  *len = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  p = malloc(*len + 1);
  if (fread(p, 1, *len, f) != *len) exit(2);
  fclose(f);
  p[*len] = 0;
  return p;
}

static void test_t2pd(const char *root) {
  char path[512];
  size_t len;
  uint8_t *d;
  t2pd_t st;

  snprintf(path, sizeof(path), "%s/user/home/10000001/trophy2/nobackup/data/NPWR90001_00/TRPTITLE.DAT", root);
  d = slurp(path, &len);
  CHECK(t2pd_parse(d, len, &st) == 0);
  CHECK(st.total == 8);
  CHECK(st.count == 3);
  CHECK(st.earned[0].id == 1 && st.earned[0].when == 1790000000);
  CHECK(st.earned[1].id == 4 && st.earned[1].when == 1790000500);
  CHECK(st.earned[2].id == 5 && st.earned[2].when == 1790001000);

  /* damaged files are refused, never read past their end */
  CHECK(t2pd_parse(d, 0x30, &st) != 0);
  CHECK(t2pd_parse(d, len - 1, &st) != 0);
  for (size_t cut = 0; cut < len; cut += 97) {
    uint8_t *copy = malloc(cut ? cut : 1);
    memcpy(copy, d, cut);
    t2pd_parse(copy, cut, &st);
    free(copy);
  }
  for (size_t i = 0; i < len; i += 7) { /* flip bytes: must not crash */
    uint8_t keep = d[i];
    d[i] ^= 0xff;
    t2pd_parse(d, len, &st);
    d[i] = keep;
  }
  d[0] = 'X';
  CHECK(t2pd_parse(d, len, &st) != 0);
  free(d);

  snprintf(path, sizeof(path), "%s/user/home/10000001/trophy2/nobackup/data/NPWR90002_00/TRPTITLE.DAT", root);
  d = slurp(path, &len);
  CHECK(t2pd_parse(d, len, &st) == 0);
  CHECK(st.total == 12 && st.count == 0);
  free(d);
}

static void test_ucp(const char *root) {
  char path[512], *buf;
  ucp_entry_t *list;
  size_t len;
  long rev = 0;
  int n;

  snprintf(path, sizeof(path), "%s/user/trophy2/nobackup/conf/NPWR90001_00/TROPHY.UCP", root);
  CHECK(ucp_list(path, &list, &n) == 0);
  CHECK(n == 4);
  CHECK(ucp_find(list, n, "tropconf.json") != NULL);
  CHECK(ucp_find(list, n, "tropmeta_pt-BR.json") != NULL);
  CHECK(ucp_find(list, n, "nope.json") == NULL);
  CHECK(ucp_read(path, ucp_find(list, n, "tropconf.json"), 1 << 20, &buf, &len) == 0);
  CHECK(buf[0] == '{' && buf[len - 1] == '}');
  CHECK(json_long(buf, "trophyDefinitionRevision", &rev) == 0 && rev == 100);
  free(buf);
  CHECK(ucp_read(path, ucp_find(list, n, "tropconf.json"), 10, &buf, &len) != 0); /* over the cap */
  CHECK(ucp_read(path, NULL, 1 << 20, &buf, &len) != 0);
  free(list);
  CHECK(ucp_list("/nonexistent/TROPHY.UCP", &list, &n) != 0);
  snprintf(path, sizeof(path), "%s/user/home/10000001/trophy2/nobackup/data/NPWR90001_00/TRPTITLE.DAT", root);
  CHECK(ucp_list(path, &list, &n) != 0); /* not an archive */
}

static void test_json(void) {
  const char *j = "{ \"ok\": true, \"need\": [\"NPWR90001_00\", \"NPWR90002_00\"], \"n\": -42,"
                  " \"player\": {\"id\":\"7K3M9QXZ\", \"nickname\": \"A \\\"b\\\" \\u00e9\\n\", \"deep\": {\"x\": [1, {\"y\": 2}], \"z\": 7}},"
                  " \"empty\": [], \"url\": \"https:\\/\\/x.test\\/u\\/1\" }";
  char buf[64];
  const char *el;
  long v = 0;
  int n = 0;

  CHECK(json_str(j, "player.id", buf, sizeof(buf)) == 0 && !strcmp(buf, "7K3M9QXZ"));
  CHECK(json_str(j, "player.nickname", buf, sizeof(buf)) == 0 && !strcmp(buf, "A \"b\" \xc3\xa9\n"));
  CHECK(json_str(j, "url", buf, sizeof(buf)) == 0 && !strcmp(buf, "https://x.test/u/1"));
  CHECK(json_long(j, "n", &v) == 0 && v == -42);
  CHECK(json_long(j, "player.deep.z", &v) == 0 && v == 7);
  CHECK(json_long(j, "player.id", &v) != 0);
  CHECK(json_str(j, "missing", buf, sizeof(buf)) != 0 && buf[0] == 0);
  CHECK(json_str(j, "player.missing.x", buf, sizeof(buf)) != 0);
  CHECK(json_str(j, "n", buf, sizeof(buf)) != 0);
  for (el = json_first(json_find(j, "need")); el; el = json_next(el)) {
    CHECK(json_copy(el, buf, sizeof(buf)) == 0);
    CHECK(!strncmp(buf, "NPWR9000", 8));
    n++;
  }
  CHECK(n == 2);
  CHECK(json_first(json_find(j, "empty")) == NULL);
  CHECK(json_first(json_find(j, "nothing")) == NULL);
  CHECK(json_str(j, "player.id", buf, 4) == 0 && !strcmp(buf, "7K3")); /* cut, still terminated */
  CHECK(json_find("not json", "a") == NULL);
  CHECK(json_find("{\"a\": \"unterminated", "a") != NULL);
  CHECK(json_find("{\"a\": {\"b\": [1,2", "a.c") == NULL);

  CHECK(json_quote(buf, sizeof(buf), "PS5 - \"Bob\"\\\t") > 0 && !strcmp(buf, "\"PS5 - \\\"Bob\\\"\\\\\\u0009\""));
}

/* Width and height from a JPEG's frame header, or 0 x 0. */
static void jpeg_size(const unsigned char *j, size_t len, int *w, int *h) {
  *w = *h = 0;
  for (size_t i = 2; i + 9 < len;) {
    if (j[i] != 0xff) return;
    if (j[i + 1] == 0xc0 || j[i + 1] == 0xc2) {
      *h = (j[i + 5] << 8) | j[i + 6];
      *w = (j[i + 7] << 8) | j[i + 8];
      return;
    }
    i += 2 + (size_t)((j[i + 2] << 8) | j[i + 3]);
  }
}

static void test_icon(const char *root) {
  char path[512];
  unsigned char *jpg;
  uint8_t *png;
  size_t len, jlen;
  int w, h;

  snprintf(path, sizeof(path), "%s/pics/square.png", root);
  png = slurp(path, &len);
  CHECK(icon_make(png, len, ICON_SIDE, &jpg, &jlen) == 0);
  CHECK(jlen > 400 && jlen < 40000);
  CHECK(jpg[0] == 0xff && jpg[1] == 0xd8 && jpg[jlen - 2] == 0xff && jpg[jlen - 1] == 0xd9);
  jpeg_size(jpg, jlen, &w, &h);
  CHECK(w == 192 && h == 192);
  free(jpg);

  /* damaged pictures are refused, never read past their end */
  for (size_t cut = 0; cut < len; cut += len / 23 + 1) {
    uint8_t *copy = malloc(cut ? cut : 1);
    memcpy(copy, png, cut);
    if (icon_make(copy, cut, ICON_SIDE, &jpg, &jlen) == 0) free(jpg);
    free(copy);
  }
  for (size_t i = 8; i < len; i += len / 61 + 1) {
    uint8_t keep = png[i];
    png[i] ^= 0x5a;
    if (icon_make(png, len, ICON_SIDE, &jpg, &jlen) == 0) free(jpg);
    png[i] = keep;
  }
  CHECK(icon_make((const unsigned char *)"not a picture at all", 20, ICON_SIDE, &jpg, &jlen) != 0);
  CHECK(icon_make(png, 0, ICON_SIDE, &jpg, &jlen) != 0);
  free(png);

  snprintf(path, sizeof(path), "%s/pics/wide.png", root);
  png = slurp(path, &len);
  CHECK(icon_make(png, len, ICON_SIDE, &jpg, &jlen) == 0);
  jpeg_size(jpg, jlen, &w, &h);
  CHECK(w == 192 && h == 96); /* the shape is kept */
  free(jpg);
  free(png);

  snprintf(path, sizeof(path), "%s/pics/small.png", root);
  png = slurp(path, &len);
  CHECK(icon_make(png, len, ICON_SIDE, &jpg, &jlen) == 0);
  jpeg_size(jpg, jlen, &w, &h);
  CHECK(w == 64 && h == 48); /* never enlarged */
  free(jpg);
  free(png);
}

int main(int argc, char **argv) {
  if (argc < 2) { printf("usage: unit <fake root>\n"); return 2; }
  test_t2pd(argv[1]);
  test_ucp(argv[1]);
  test_json();
  test_icon(argv[1]);
  printf(g_fail ? "%d check(s) failed\n" : "unit tests passed\n", g_fail);
  return g_fail ? 1 : 0;
}
