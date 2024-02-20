///////////////////////////////////////////////////////////////////////////////
///
/// Interface
///

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct map {
    void *data;
    uint64_t (*hash)(void const *, size_t, uint64_t);
    bool (*equal)(void const *, void const *);
    size_t cap, count, stride, key_stride, val_stride;
    uint64_t seed;
} map_t;

#define MAP(K, V, HASH, EQUAL, CAP, SEED)\
    (struct map){.key_stride = sizeof (K), .val_stride = sizeof (V), .hash = (HASH), .equal = (EQUAL), .cap = (CAP), .seed = (SEED)}

bool  map_init (struct map *m);
bool  map_set  (struct map *m, void const *key, void const *val);
void *map_get  (struct map *m, void const *key);
bool  map_rem  (struct map *m, void const *key);
void  map_free (struct map *m);

///////////////////////////////////////////////////////////////////////////////
///
/// Implementation
///

#if defined(MAP_IMPL)

#include <stdlib.h>
#include <string.h>

// --- Internal functions --- //

static uint64_t *_map_get_slot(struct map *m, size_t i) {
    return (uint64_t *)&((uint8_t *)m->data)[i * m->stride];
}

static uint64_t _map_get_hash(uint64_t const *item) {
    return *item & 0xffffffffffffff00;
}

static uint8_t _map_get_dist(uint64_t const *item) {
    return *item & 0x00000000000000ff;
}

static void _map_set_dist(uint64_t *item, uint8_t dist) {
    *item = _map_get_hash(item) | dist;
}

static void _map_swap(struct map *m, void *a, void *b) {
    void *t = _map_get_slot(m, m->cap + 1);
    memcpy(t, a, m->stride);
    memcpy(a, b, m->stride);
    memcpy(b, t, m->stride);
}

// Resize the map
static bool _map_resize(struct map *m, size_t cap) {
    struct map m2 = *m;
    m2.cap = cap;
    if (!map_init(&m2))
        return false;

    // Recalculate the slot for each item in m and insert it into m2
    for (size_t i = 0; i < m->cap; i++) {
        uint64_t *item = _map_get_slot(m, i);
        if (_map_get_dist(item) == 0) continue;
        _map_set_dist(item, 1);

        // item->hash & (m2.cap - 1) is the same as doing item->hash % m2.cap since m2.cap is always a power of 2
        for (size_t j = _map_get_hash(item) & (m2.cap - 1); ; _map_set_dist(item, _map_get_dist(item) + 1), j = (j + 1) & (m2.cap - 1)) {
            uint64_t *slot = _map_get_slot(&m2, j);

            // A distance of 0 represents no value, so this slot is free
            if (_map_get_dist(slot) == 0) {
                memcpy(slot, item, m->stride);
                break;
            }

            // Check the distance of the current item against the current slot. If a slot is found that has a greater desired distance
            // than the current item, swap them. Now we are inserting the item that was already in the map.
            if (_map_get_dist(slot) < _map_get_dist(item))
                _map_swap(m, slot, item);
        }
    }

    free(m->data);
    *m = m2;
    return true;
}

// --- API functions --- //

bool map_init(struct map *m) {
    if (m->key_stride == 0 || m->val_stride == 0 || m->hash == NULL || m->equal == NULL)
        return false;

    size_t cap = 8;
    for (; cap < m->cap; cap *= 2);

    m->cap = cap;
    m->stride = m->key_stride + m->val_stride + sizeof (uint64_t);
    m->data = calloc(m->cap + 2, m->stride); // Allocate space for two items past the end of the array, these will be used as staging values

    return (m->data != NULL);
}

// Copys key and val into the map. Returns true if the copy was successful, false otherwise
bool map_set(struct map *m, void const *key, void const *val) {
    if (m->count * 4 == m->cap * 3)
        if (!_map_resize(m, m->cap * 2))
            return false;

    // Load key and val into the temporary slot
    uint64_t *item = _map_get_slot(m, m->cap);
    *item = (m->hash(key, m->key_stride, m->seed) & 0xffffffffffffff00) | 1;
    memcpy(item + 1, key, m->key_stride); // Put the key data right after the meta data...
    memcpy((char *)(item + 1) + m->key_stride, val, m->val_stride); // ... and the value just past that

    for (size_t i = _map_get_hash(item) & (m->cap - 1); ; _map_set_dist(item, _map_get_dist(item) + 1), i = (i + 1) & (m->cap - 1)) {
        uint64_t *slot = _map_get_slot(m, i);

        // Insert if the slot contains no value at all or the key (updating the value)
        if (_map_get_dist(slot) == 0 || (_map_get_hash(slot) == _map_get_hash(item) && m->equal(slot + 1, key))) {
            m->count += (_map_get_dist(slot) == 0);
            memcpy(slot, item, m->stride);
            return true;
        }
        if (_map_get_dist(slot) < _map_get_dist(item))
            _map_swap(m, slot, item);
    }
}

// Returns a pointer to the corresponding value if key exists in the map, NULL if it does not
void *map_get(struct map *m, void const *key) {
    uint64_t hash = m->hash(key, m->key_stride, m->seed) & 0xffffffffffffff00;

    for (size_t i = hash & (m->cap - 1); ; i = (i + 1) & (m->cap - 1)) {
        uint64_t *slot = _map_get_slot(m, i);
        if (_map_get_dist(slot) == 0)
            return NULL;
        if (_map_get_hash(slot) == hash && m->equal(slot + 1, key))
            return (uint8_t *)(slot + 1) + m->key_stride;
    }
}

// Returns true if key exists in the map, NULL if it does not
bool map_rem(struct map *m, void const *key) {
    uint64_t hash = m->hash(key, m->key_stride, m->seed) & 0xffffffffffffff00;

    for (size_t i = hash & (m->cap - 1); ; i = (i + 1) & (m->cap - 1)) {
        uint64_t *slot = _map_get_slot(m, i);
        if (_map_get_dist(slot) == 0)
            return false;

        if (_map_get_hash(slot) == hash && m->equal(slot + 1, key)) {
            _map_set_dist(slot, 0);

            // Move every slot ahead of the removed one back one place and decrease their distances
            for (;;) {
                uint64_t *prev = slot;
                i = (i + 1) & (m->cap - 1);
                slot = _map_get_slot(m, i);
                if (_map_get_dist(slot) <= 1) {
                    _map_set_dist(prev, 0);
                    break;
                }
                memcpy(prev, slot, m->stride);
                _map_set_dist(prev, _map_get_dist(prev) - 1);
            }

            m->count--;

            // Only resize to a smaller capacity very rarely, in this case when the load is 0.1
            if (m->count * 10 == m->cap)
                _map_resize(m, m->cap / 2);

            return true;
        }
    }
}

void map_free(struct map *m) {
    if (m == NULL)
        return;

    free(m->data);
    m->data = NULL;
    m->cap = m->count = 0;
}

#endif // MAP_IMPL
