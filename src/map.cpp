#define MAP_DEPTH 4
typedef u32 Hash;

typedef b32  (*Map_Cmp_Func)(void *, void *);
typedef Hash (*Map_Hash_Func)(void *);

#define MAP_LEAF_VALUES_COUNT (1 << 8)

struct Map_Leaf {
    isize size;
    // If NULL, this leaf only contains other Map_Leaf's
    void  *keys;
    // If NULL, this leaf does not have anything more
    void  *values;
};

template <typename K, typename V>
struct Map {
    Arena         *arena;
    isize         len;
    Map_Cmp_Func  cmp_func;
    Map_Hash_Func hash_func;
    Map_Leaf      root;
};

template <typename K, typename V>
internal Map_Leaf map_alloc_map_leafs(Map<K, V> *map) {
    auto slice    = arena_alloc_slice<Map_Leaf>(&map->arena, MAP_LEAF_VALUES_COUNT);
    Map_Leaf leaf = {};
    leaf.values   = slice.ptr;
    leaf.size     = MAP_LEAF_VALUES_COUNT;
    return leaf;
}

template <typename K, typename V>
internal void map_init(Map<K, V> *map, Map_Cmp_Func cmp_func, Map_Hash_Func hash_func) {
    map->root      = map_alloc_map_leafs(map);
    map->cmp_func  = cmp_func;
    map->hash_func = hash_func;
}

template <typename K, typename V>
internal void map_insert(Map<K, V> *map, K key, V value) {
    Hash hash = map->hash_func(&key);
    Map_Leaf *leaf = &map->root;
    for (usize i = 0; i < MAP_DEPTH-1; i++) {
        u8 index = cast(u8)((hash >> cast(Hash)(i * 8)) & ~cast(Hash)0);

        Map_Leaf *new_leaf = (cast(Map_Leaf*)leaf->values)[index];
        if (new_leaf->values == NULL && new_leaf->keys == NULL) {
            *new_leaf = map_alloc_map_leafs(map);
        }
        leaf = new_leaf;
    }

    u8 index = cast(u8)((hash >> cast(Hash)((MAP_DEPTH-1) * 8)) & ~cast(Hash)0);
}
