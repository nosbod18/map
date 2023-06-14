# map

```c
#include "map.h"
#include <string.h>

uint64_t hash(void const *key, size_t stride, uint64_t seed) {
    uint64_t hash = 5381;
    for (uint8_t const *p = key; *p; p++)
        hash = ((hash << 5) + hash) ^ *p;
    return hash;
}

bool equal(void const *a, void const *b) {
    return strcmp(a, b) == 0;
}

int main(void) {
    Map m = MAP(char *, char *, hash, equal, 0, 0);
    if (!map_init(&m)) return 1;

    map_set(&m, "Hello", "World");
    char **world = map_get(&m, "Hello"); // *world == "World"

    map_rem(&m, "Hello");
    world = map_get(&m, "Hello"); // world == NULL

    map_free(&m);
}

```