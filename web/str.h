/**
 *  simple static c-style string, used with buffer_t or constant c string,
 *  no need to copy memory, just save time
 */
#pragma once

#include <string.h>
#include <strings.h>
#include <stdbool.h>

typedef struct {
  char *str;
  int len;
} ssstr;

/**
 * const char *p = "hello";
 * SSSTR("hello") ✅
 * SSSTR(p)❌
 */
#define SSSTR(cstr)                                                            \
  (ssstr) { cstr, sizeof(cstr) - 1 }

static inline void ssstr_init(ssstr *s) {
  s->str = NULL;
  s->len = 0;
}

static inline void ssstr_set(ssstr *s, const char *cstr) {
  s->str = (char *)cstr;
  s->len = strlen(cstr);
}

extern void ssstr_print(const ssstr *s);
extern void ssstr_tolower(ssstr *s);
extern int ssstr_cmp(const ssstr *l, const ssstr *r);
extern bool ssstr_equal(const ssstr *s, const char *cstr);

static inline bool ssstr_caseequal(ssstr *s, const char *cstr) {
  return strncasecmp(s->str, cstr, strlen(cstr)) == 0 ? true : false;
}

