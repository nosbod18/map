#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t map_hash_fn (void const *, uint64_t);
typedef bool     map_equal_fn(void const *, void const *);

struct map {
    void *slots, *item, *swap;
    size_t cap, count, stride;
    map_hash_fn *hash;
    map_equal_fn *equal;
    uint64_t seed;
    bool oom;
};

struct map  *map_new  (size_t stride, size_t cap, uint64_t seed, map_hash_fn *hash, map_equal_fn *equal);
bool         map_set  (struct map *m, void const *data);
void        *map_get  (struct map *m, void const *data);
bool         map_rem  (struct map *m, void const *data);
void         map_free (struct map *m);
