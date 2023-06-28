///////////////////////////////////////////////////////////////////////////////
///
/// Interface
///

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
} map_t;

#define MAP(K, V, HASH, EQUAL, CAP, SEED)\
    (map_t){.key_stride = sizeof (K), .val_stride = sizeof (V), .hash = (HASH), .equal = (EQUAL), .cap = (CAP), .seed = (SEED)}

bool  map_init (map_t *m);
bool  map_set  (map_t *m, void const *key, void const *val);
void *map_get  (map_t *m, void const *key);
bool  map_rem  (map_t *m, void const *key);
void  map_free (map_t *m);

///////////////////////////////////////////////////////////////////////////////
///
/// Implementation
///

#if defined(MAP_IMPL)

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t hash: 56;
    uint64_t dist: 8;
} _map_slot_t;

static _map_slot_t *_map_get_slot(map_t *m, size_t i) {
    return (_map_slot_t *)&((char *)m->slots)[i * m->stride];
}

static void _map_load_kv(map_t *m, void const *key, void const *val) {
    _map_slot_t *item = m->item;
    item->hash = m->hash(key, m->key_stride, m->seed) & 0x00ffffffffffffff;
    item->dist = 1;
    memcpy(item + 1, key, m->key_stride);
    memcpy((char *)(item + 1) + m->key_stride, val, m->val_stride);
}

static bool resize(map_t *m, size_t cap) {
    map_t m2 = *m;
    m2.cap = cap;
    if (!map_init(&m2))
        return false;

    for (size_t i = 0; i < m->cap; i++) {
        _map_slot_t *item = _map_get_slot(m, i);
        if (item->dist == 0) continue;
        item->dist = 1;
        for (size_t j = item->hash & (m2.cap - 1); ; item->dist++, j = (j + 1) & (m2.cap - 1)) {
            _map_slot_t *slot = _map_get_slot(&m2, j);
            if (slot->dist == 0) {
                memcpy(slot, item, m->stride);
                break;
            }
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

bool map_init(map_t *m) {
    if (m->key_stride == 0 || m->val_stride == 0 || m->hash == NULL || m->equal == NULL)
        return false;

    size_t cap = 16;
    for (; cap < m->cap; cap *= 2);

    m->cap = cap;
    m->stride = m->key_stride + m->val_stride + sizeof (_map_slot_t);
    m->slots = calloc(m->cap + 2, m->stride);

    if (m->slots == NULL)
        return false;

    m->item = _map_get_slot(m, m->cap);
    m->swap = _map_get_slot(m, m->cap + 1);
    return true;
}

bool map_set(map_t *m, void const *key, void const *val) {
    if (m->count * 4 == m->cap * 3)
        if (!resize(m, m->cap * 2))
            return NULL;

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

void *map_get(map_t *m, void const *data) {
    uint64_t hash = m->hash(data, m->key_stride, m->seed) & 0x00ffffffffffffff;
    for (size_t i = hash & (m->cap - 1); ; i = (i + 1) & (m->cap - 1)) {
        _map_slot_t *slot = _map_get_slot(m, i);
        if (slot->dist == 0)
            return NULL;
        if (slot->hash == hash && m->equal(slot + 1, data))
            return slot + 1;
    }
}

bool map_rem(map_t *m, void const *data) {
    uint64_t hash = m->hash(data, m->key_stride, m->seed) & 0x00ffffffffffffff;
    for (size_t i = hash & (m->cap - 1); ; i = (i + 1) & (m->cap - 1)) {
        _map_slot_t *slot = _map_get_slot(m, i);
        if (slot->dist == 0)
            return false;
        if (slot->hash == hash && m->equal(slot + 1, data)) {
            slot->dist = 0;
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
                resize(m, m->cap / 2);
            return true;
        }
    }
}

void map_free(map_t *m) {
    if (m == NULL)
        return;

    free(m->slots);
    m->cap = m->count = 0;
    m->item = m->swap = NULL;
}

#endif // MAP_IMPL
