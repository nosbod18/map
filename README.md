# map
An extremely fast hash table with robin hood based hashing.

```c
#define MAP_IMPL
#include "map.h"
#include <string.h>
#include <stdio.h>

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
    map_t m = MAP(char *, char *, hash, equal, 0, 0);
    if (!map_init(&m)) return 1;

    char const *key = "Hello";
    char const *val = "World";
    char **get = NULL;

    map_set(&m, &key, &val);
    get = map_get(&m, &key);
    printf("Hello = %s\n", get ? *get : "<null>");

    map_rem(&m, &key);
    get = map_get(&m, &key);
    printf("%s = %s\n", key, get ? *get : "<null>");

    map_free(&m);
}

// Output:
//  Hello = World
//  Hello = <null>
```