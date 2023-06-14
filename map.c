#include "map.h"
#include <stdlib.h>
#include <string.h>

#define HASH_CLIP (0x00ffffffffffffff)

typedef struct {
    uint64_t hash: 56;
    uint64_t dist: 8;
} MapHeader;

static MapHeader *get_slot(Map *m, size_t i) {
    return (MapHeader *)&((char *)m->slots)[i * m->stride];
}

static void load_keyval(Map *m, void const *key, void const *val) {
    MapHeader *item = m->item;
    item->hash = m->hash(key, m->key_stride, m->seed) & HASH_CLIP;
    item->dist = 1;
    memcpy(item + 1, key, m->key_stride);
    memcpy((char *)(item + 1) + m->key_stride, val, m->val_stride);
}

static bool resize(Map *m, size_t cap) {
    Map m2 = *m;
    m2.cap = cap;
    if (!map_init(&m2))
        return false;

    for (size_t i = 0; i < m->cap; i++) {
        MapHeader *item = get_slot(m, i);
        if (item->dist == 0) continue;
        item->dist = 1;
        for (size_t j = item->hash & (m2.cap - 1); ; item->dist++, j = (j + 1) & (m2.cap - 1)) {
            MapHeader *slot = get_slot(&m2, j);
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

bool map_init(Map *m) {
    if (m->key_stride == 0 || m->val_stride == 0 || m->hash == NULL || m->equal == NULL)
        return false;

    size_t cap = 16;
    for (; cap < m->cap; cap *= 2);

    m->cap = cap;
    m->stride = m->key_stride + m->val_stride + sizeof (MapHeader);
    m->slots = calloc(m->cap + 2, m->stride);

    if (m->slots == NULL)
        return false;

    m->item = get_slot(m, m->cap);
    m->swap = get_slot(m, m->cap + 1);
    return true;
}

bool map_set(Map *m, void const *key, void const *val) {
    if (m->count * 4 == m->cap * 3)
        if (!resize(m, m->cap * 2))
            return NULL;

    load_keyval(m, key, val);
    MapHeader *item = m->item;

    for (size_t i = item->hash & (m->cap - 1); ; item->dist++, i = (i + 1) & (m->cap - 1)) {
        MapHeader *slot = get_slot(m, i);
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

void *map_get(Map *m, void const *data) {
    uint64_t hash = m->hash(data, m->key_stride, m->seed) & HASH_CLIP;
    for (size_t i = hash & (m->cap - 1); ; i = (i + 1) & (m->cap - 1)) {
        MapHeader *slot = get_slot(m, i);
        if (slot->dist == 0)
            return NULL;
        if (slot->hash == hash && m->equal(slot + 1, data))
            return slot + 1;
    }
}

bool map_rem(Map *m, void const *data) {
    uint64_t hash = m->hash(data, m->key_stride, m->seed) & HASH_CLIP;
    for (size_t i = hash & (m->cap - 1); ; i = (i + 1) & (m->cap - 1)) {
        MapHeader *slot = get_slot(m, i);
        if (slot->dist == 0)
            return false;
        if (slot->hash == hash && m->equal(slot + 1, data)) {
            slot->dist = 0;
            for (;;) {
                MapHeader *prev = slot;
                i = (i + 1) & (m->cap - 1);
                slot = get_slot(m, i);
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

void map_free(Map *m) {
    if (m == NULL)
        return;

    free(m->slots);
    m->cap = m->count = 0;
    m->item = m->swap = NULL;
}