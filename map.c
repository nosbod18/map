#include "map.h"
#include <stdlib.h>
#include <string.h>

#define HASH_CLIP (0x00ffffffffffffff)

struct header {
    uint64_t hash: 56;
    uint64_t dist: 8;
};

static struct header *get_slot(struct map *m, size_t i) {
    return (struct header *)&((char *)m->slots)[i * m->stride];
}

static bool resize(struct map *m, size_t cap) {
    struct map *m2 = map_new(m->stride, cap, m->seed, m->hash, m->equal);
    if (m2 == NULL) return false;

    for (size_t i = 0; i < m->cap; i++) {
        struct header *item = get_slot(m, i);
        if (item->dist == 0) continue;
        item->dist = 1;
        for (size_t j = item->hash & (m2->cap - 1); ; item->dist++, j = (j + 1) & (m2->cap - 1)) {
            struct header *slot = get_slot(m2, j);
            if (slot->dist == 0) {
                memcpy(slot, item, m->stride);
                m2->count++;
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
    *m = *m2;
    free(m2);
    return true;
}

struct map *map_new(size_t stride, size_t cap, uint64_t seed, map_hash_fn *hash, map_equal_fn *equal) {
    if (stride == 0 || hash == NULL || equal == NULL)
        return NULL;

    size_t ncap = 16;
    for (; ncap < cap; ncap *= 2);

    cap = ncap;
    stride += sizeof (struct header);

    struct map *m = malloc(sizeof *m);
    if (m == NULL) return NULL;

    *m = (struct map) {
        .slots  = calloc(cap + 2, stride),
        .cap    = cap,
        .stride = stride,
        .hash   = hash,
        .equal  = equal,
        .seed   = seed
    };
    if (m->slots == NULL) {
        free(m);
        return NULL;
    }

    m->item = get_slot(m, m->cap);
    m->swap = get_slot(m, m->cap + 1);
    return m;
}

bool map_set(struct map *m, void const *data) {
    if (m->count * 4 == m->cap * 3)
        if (!resize(m, m->cap * 2))
            return NULL;
    struct header *item = m->item;
    item->hash = m->hash(data, m->seed) & HASH_CLIP;
    item->dist = 1;
    memcpy(item + 1, data, m->stride - sizeof *item);
    for (size_t i = item->hash & (m->cap - 1); ; item->dist++, i = (i + 1) & (m->cap - 1)) {
        struct header *slot = get_slot(m, i);
        if (slot->dist == 0 || (slot->hash == item->hash && m->equal(slot + 1, data))) {
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

void *map_get(struct map *m, void const *data) {
    uint64_t hash = m->hash(data, m->seed) & HASH_CLIP;
    for (size_t i = hash & (m->cap - 1); ; i = (i + 1) & (m->cap - 1)) {
        struct header *slot = get_slot(m, i);
        if (slot->dist == 0)
            return NULL;
        if (slot->hash == hash && m->equal(slot + 1, data))
            return slot + 1;
    }
}

bool map_rem(struct map *m, void const *data) {
    uint64_t hash = m->hash(data, m->seed) & HASH_CLIP;
    for (size_t i = hash & (m->cap - 1); ; i = (i + 1) & (m->cap - 1)) {
        struct header *slot = get_slot(m, i);
        if (slot->dist == 0)
            return false;
        if (slot->hash == hash && m->equal(slot + 1, data)) {
            slot->dist = 0;
            for (;;) {
                struct header *prev = slot;
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

void map_free(struct map *m) {
    if (m == NULL)
        return;
    free(m->slots);
    free(m);
}

//////////////////////////////////////////////////////////////////////////////
/// Test

#include <assert.h>
#include <stdio.h>
#include <time.h>

#define BENCH(label, count, ...) do {\
    clock_t start = clock();\
    for (int i = 0; i < (count); i++) {\
        __VA_ARGS__;\
    }\
    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;\
    printf("%-16s %fs %.0fns/op\n", label, elapsed, (elapsed * 1.0e9) / (double)(count));\
} while (0)

#define XXH_PRIME_1 11400714785074694791ULL
#define XXH_PRIME_2 14029467366897019727ULL
#define XXH_PRIME_3 1609587929392839161ULL
#define XXH_PRIME_4 9650029242287828579ULL
#define XXH_PRIME_5 2870177450012600261ULL

static uint64_t XXH_read64(const void* memptr) {
    uint64_t val;
    memcpy(&val, memptr, sizeof(val));
    return val;
}

static uint32_t XXH_read32(const void* memptr) {
    uint32_t val;
    memcpy(&val, memptr, sizeof(val));
    return val;
}

static uint64_t XXH_rotl64(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

static uint64_t xxh3(const void* data, size_t len, uint64_t seed) {
    const uint8_t* p = (const uint8_t*)data;
    const uint8_t* const end = p + len;
    uint64_t h64;
    if (len >= 32) {
        const uint8_t* const limit = end - 32;
        uint64_t v1 = seed + XXH_PRIME_1 + XXH_PRIME_2;
        uint64_t v2 = seed + XXH_PRIME_2;
        uint64_t v3 = seed + 0;
        uint64_t v4 = seed - XXH_PRIME_1;
        do {
            v1 += XXH_read64(p) * XXH_PRIME_2;
            v1 = XXH_rotl64(v1, 31);
            v1 *= XXH_PRIME_1;
            v2 += XXH_read64(p + 8) * XXH_PRIME_2;
            v2 = XXH_rotl64(v2, 31);
            v2 *= XXH_PRIME_1;
            v3 += XXH_read64(p + 16) * XXH_PRIME_2;
            v3 = XXH_rotl64(v3, 31);
            v3 *= XXH_PRIME_1;
            v4 += XXH_read64(p + 24) * XXH_PRIME_2;
            v4 = XXH_rotl64(v4, 31);
            v4 *= XXH_PRIME_1;
            p += 32;
        } while (p <= limit);
        h64 = XXH_rotl64(v1, 1) + XXH_rotl64(v2, 7) + XXH_rotl64(v3, 12) + 
            XXH_rotl64(v4, 18);
        v1 *= XXH_PRIME_2;
        v1 = XXH_rotl64(v1, 31);
        v1 *= XXH_PRIME_1;
        h64 ^= v1;
        h64 = h64 * XXH_PRIME_1 + XXH_PRIME_4;
        v2 *= XXH_PRIME_2;
        v2 = XXH_rotl64(v2, 31);
        v2 *= XXH_PRIME_1;
        h64 ^= v2;
        h64 = h64 * XXH_PRIME_1 + XXH_PRIME_4;
        v3 *= XXH_PRIME_2;
        v3 = XXH_rotl64(v3, 31);
        v3 *= XXH_PRIME_1;
        h64 ^= v3;
        h64 = h64 * XXH_PRIME_1 + XXH_PRIME_4;
        v4 *= XXH_PRIME_2;
        v4 = XXH_rotl64(v4, 31);
        v4 *= XXH_PRIME_1;
        h64 ^= v4;
        h64 = h64 * XXH_PRIME_1 + XXH_PRIME_4;
    }
    else {
        h64 = seed + XXH_PRIME_5;
    }
    h64 += (uint64_t)len;
    while (p + 8 <= end) {
        uint64_t k1 = XXH_read64(p);
        k1 *= XXH_PRIME_2;
        k1 = XXH_rotl64(k1, 31);
        k1 *= XXH_PRIME_1;
        h64 ^= k1;
        h64 = XXH_rotl64(h64, 27) * XXH_PRIME_1 + XXH_PRIME_4;
        p += 8;
    }
    if (p + 4 <= end) {
        h64 ^= (uint64_t)(XXH_read32(p)) * XXH_PRIME_1;
        h64 = XXH_rotl64(h64, 23) * XXH_PRIME_2 + XXH_PRIME_3;
        p += 4;
    }
    while (p < end) {
        h64 ^= (*p) * XXH_PRIME_5;
        h64 = XXH_rotl64(h64, 11) * XXH_PRIME_1;
        p++;
    }
    h64 ^= h64 >> 33;
    h64 *= XXH_PRIME_2;
    h64 ^= h64 >> 29;
    h64 *= XXH_PRIME_3;
    h64 ^= h64 >> 32;
    return h64;
}

uint64_t hash(void const *data, uint64_t seed) {
    return xxh3(data, sizeof (int), seed);
}

bool equal(void const *a, void const *b) {
    return *(int const *)a == *(int const *)b;
}

void test(size_t count, size_t cap, uint64_t seed, bool *res, int *val) {
    printf("count=%zu, cap=%zu, seed=%llu\n", count, cap, seed);

    struct map *m = map_new(sizeof (int), cap, seed, hash, equal);
    assert(m);

    BENCH("set (existn't)", count, {
        *res = map_set(m, &i);
        assert(*res);
    });

    BENCH("get (exists)", count, {
        val = map_get(m, &i);
        assert(val && *val == i);
    });

    BENCH("set (exists)", count, {
        *res = map_set(m, &i);
        assert(*res);
    });

    BENCH("rem (exists)", count, {
        *res = map_rem(m, &i);
        assert(*res);
    });

    BENCH("get (existn't)", count, {
        val = map_get(m, &(int){i + count});
        assert(!val);
    });

    BENCH("rem (existn't)", count, {
        *res = map_rem(m, &(int){i + count});
        assert(!*res);
    });

    map_free(m);
}

int main(void) {
    uint64_t seed  = getenv("seed")  ? atoi(getenv("seed"))  : time(NULL);
    size_t   count = getenv("count") ? atoi(getenv("count")) : 5000000;

    bool res = false;
    int val = 0;

    test(count, count, seed, &res, &val);
    printf("%d, %d\n\n", res, val);
    test(count, 0, seed, &res, &val);
    printf("%d, %d\n", res, val);
}
