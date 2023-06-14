#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *slots, *item, *swap;
    uint64_t (*hash)(void const *, size_t, uint64_t);
    bool (*equal)(void const *, void const *);
    size_t cap, count, stride, key_stride, val_stride;
    uint64_t seed;
} Map;

#define MAP(K, V, HASH, EQUAL, CAP, SEED)\
    (Map){.key_stride = sizeof (K), .val_stride = sizeof (V), .hash = (HASH), .equal = (EQUAL), .cap = (CAP), .seed = (SEED)}

bool  map_init (Map *m);
bool  map_set  (Map *m, void const *key, void const *val);
void *map_get  (Map *m, void const *key);
bool  map_rem  (Map *m, void const *key);
void  map_free (Map *m);
