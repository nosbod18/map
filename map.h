///////////////////////////////////////////////////////////////////////////////
///
/// Interface
///

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct map {
    void *slots, *item, *swap;
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

// The meta data of each key-val pair in the map
typedef struct {
    uint64_t hash: 56; // The key's hash
    uint64_t dist: 8;  // The number of slots from this item's ideal position
} _map_slot_t;

// Utility function to access an item
static _map_slot_t *_map_get_slot(struct map *m, size_t i) {
    return (_map_slot_t *)&((char *)m->slots)[i * m->stride];
}

// Copys the passed item into the map's item field
static void _map_load_kv(struct map *m, void const *key, void const *val) {
    _map_slot_t *item = m->item;
    item->hash = m->hash(key, m->key_stride, m->seed) & 0x00ffffffffffffff;
    item->dist = 1;
    memcpy(item + 1, key, m->key_stride); // Put the key data right after the meta data...
    memcpy((char *)(item + 1) + m->key_stride, val, m->val_stride); // ... and the value just past that
}

// Resize the map
static bool _map_resize(struct map *m, size_t cap) {
    struct map m2 = *m;
    m2.cap = cap;
    if (!map_init(&m2))
        return false;

    // Recalculate the slot for each item in m and insert it into m2
    for (size_t i = 0; i < m->cap; i++) {
        _map_slot_t *item = _map_get_slot(m, i);
        if (item->dist == 0) continue;
        item->dist = 1;

        // item->hash & (m2.cap - 1) is the same as doing item->hash % m2.cap since m2.cap is always a power of 2
        for (size_t j = item->hash & (m2.cap - 1); ; item->dist++, j = (j + 1) & (m2.cap - 1)) {
            _map_slot_t *slot = _map_get_slot(&m2, j);

            // A distance of 0 represents no value, so copy the item into the map
            if (slot->dist == 0) {
                memcpy(slot, item, m->stride);
                break;
            }

            // Check the distance of the current item against the current slot. If a slot is found that has a distance greater
            // than the current item, swap them. Now we are inserting the item that was already in the map.
            if (slot->dist < item->dist) {
                memcpy(m->swap, slot, m->stride);
                memcpy(slot, item, m->stride);
                memcpy(item, m->swap, m->stride);
            }
        }
    }

    free(m->slots);
    *m = m2;
    return true;
}

bool map_init(struct map *m) {
    if (m->key_stride == 0 || m->val_stride == 0 || m->hash == NULL || m->equal == NULL)
        return false;

    size_t cap = 16;
    for (; cap < m->cap; cap *= 2);

    m->cap = cap;
    m->stride = m->key_stride + m->val_stride + sizeof (_map_slot_t);
    m->slots = calloc(m->cap + 2, m->stride); // Allocate space for the array and the item and swap fields

    if (m->slots == NULL)
        return false;

    // Initialize the item and swap fields to the slots just past the end of the usable array
    m->item = _map_get_slot(m, m->cap);
    m->swap = _map_get_slot(m, m->cap + 1);
    return true;
}

// Copys key and val into the map. Returns true if the copy was successfull, false otherwise
bool map_set(struct map *m, void const *key, void const *val) {
    if (m->count * 4 == m->cap * 3)
        if (!_map_resize(m, m->cap * 2))
            return false;

    _map_load_kv(m, key, val);
    _map_slot_t *item = m->item;

    for (size_t i = item->hash & (m->cap - 1); ; item->dist++, i = (i + 1) & (m->cap - 1)) {
        _map_slot_t *slot = _map_get_slot(m, i);
        if (slot->dist == 0 || (slot->hash == item->hash && m->equal(slot + 1, key))) {
            m->count += (slot->dist == 0);
            memcpy(slot, item, m->stride);
            return true;
        }
        if (slot->dist < item->dist) {
            memcpy(m->swap, slot, m->stride);
            memcpy(slot, item, m->stride);
            memcpy(item, m->swap, m->stride);
        }
    }
}

// Returns a pointer to the corresponding value if key exists in the map, NULL if it does not
void *map_get(struct map *m, void const *key) {
    uint64_t hash = m->hash(key, m->key_stride, m->seed) & 0x00ffffffffffffff;

    for (size_t i = hash & (m->cap - 1); ; i = (i + 1) & (m->cap - 1)) {
        _map_slot_t *slot = _map_get_slot(m, i);
        if (slot->dist == 0)
            return NULL;
        if (slot->hash == hash && m->equal(slot + 1, key))
            return slot + 1;
    }
}

// Returns true if key exists in the map, NULL if it does not
bool map_rem(struct map *m, void const *key) {
    uint64_t hash = m->hash(key, m->key_stride, m->seed) & 0x00ffffffffffffff;

    for (size_t i = hash & (m->cap - 1); ; i = (i + 1) & (m->cap - 1)) {
        _map_slot_t *slot = _map_get_slot(m, i);
        if (slot->dist == 0)
            return false;

        if (slot->hash == hash && m->equal(slot + 1, key)) {
            slot->dist = 0;

            // Move every slot ahead of the removed one back one place and decrease its distance
            for (;;) {
                _map_slot_t *prev = slot;
                i = (i + 1) & (m->cap - 1);
                slot = _map_get_slot(m, i);
                if (slot->dist <= 1) {
                    prev->dist = 0;
                    break;
                }
                memcpy(prev, slot, m->stride);
                prev->dist--;
            }

            m->count--;
            if (m->count * 10 == m->cap)
                _map_resize(m, m->cap / 2);

            return true;
        }
    }
}

void map_free(struct map *m) {
    if (m == NULL)
        return;

    free(m->slots);
    m->cap = m->count = 0;
    m->item = m->swap = NULL;
}

#endif // MAP_IMPL
