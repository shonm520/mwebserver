
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include "dict.h"
#include "str.h"

// https://www.byvoid.com/zhs/blog/string-hash-compare
static unsigned int ssstr_hash_sdbm(const ssstr *key) 
{
  const char *str = key->str;
  unsigned int hash = 0;
  while (str != (key->str + key->len)) {
    // equivalent to: hash = 65599*hash + (*str++);
    hash = (*str++) + (hash << 6) + (hash << 16) - hash;
  }
  return (hash & 0x7FFFFFFF);
}

#define LOTOS_HASH(key) (ssstr_hash_sdbm(key) & DICT_MASK_SIZE)

void dict_put(dict_t *dict, const ssstr *key, void *val)
{
  unsigned int hash = LOTOS_HASH(key);
  dict_node_t *p = dict->table[hash];
  dict_node_t *q = (dict_node_t*)malloc(sizeof(dict_node_t));

  // p == NULL or p != NULL, the same
  dict_node_init(q, (ssstr *)key, val, p);
  dict->table[hash] = q;

  dict->used++;
}

/* cause we can put NULL as a valid value, so found_flag is necessary */
void *dict_get(dict_t *dict, const ssstr *key, bool *found_flag)
{
  if (found_flag != NULL)
    *found_flag = false;
  unsigned int hash = LOTOS_HASH(key);
  dict_node_t *p = dict->table[hash];

  while (p != NULL) {
    if (ssstr_cmp(&p->k, key) == 0) {
      if (found_flag != NULL)
        *found_flag = true;
      return p->v;
    }
    p = p->next;
  }

  return NULL;
}

void dict_free(dict_t *d)
{
  if (d == NULL)
    return;
  int i;
  for (i = 0; i < sizeof(d->table) / sizeof(dict_node_t *); i++) {
    dict_node_t *p = d->table[i];
    while (p != NULL) {
      dict_node_t *q = p->next;
      free(p);
      d->used--;
      p = q;
    }
  }
  assert(d->used == 0);
}
