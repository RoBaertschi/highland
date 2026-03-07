#define MAP_DEPTH 4
typedef u32 Hash;

typedef b32  (*Map_Cmp_Func)(void *, void *);
typedef Hash (*Map_Hash_Func)(void *);

#define MAP_LEAF_VALUES_COUNT (1 << 8)

struct Map_Leaf {
    // isize size;
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
internal void map_leaf_init_leafs(Map<K, V> *map, Map_Leaf *leaf) {
    auto slice   = arena_alloc_slice<Map_Leaf>(&map->arena, MAP_LEAF_VALUES_COUNT);
    *leaf        = {};
    leaf->values = slice.ptr;
}

template <typename K, typename V>
internal void map_leaf_init_key_values(Map<K, V> *map, Map_Leaf *leaf) {
    auto keys    = arena_alloc_slice<K>(&map->arena, MAP_LEAF_VALUES_COUNT);
    auto values  = arena_alloc_slice<V>(&map->arena, MAP_LEAF_VALUES_COUNT);
    *leaf        = {};
    leaf->keys   = keys.ptr;
    leaf->values = values.ptr;
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
    for (usize i = 0; i < MAP_DEPTH-2 /* Root already found and last level are the actual values, not a new leaf. */; i++) {
        u8 index = cast(u8)((hash >> cast(Hash)(i * 8)) & ~cast(Hash)0);

        Map_Leaf *new_leaf = (cast(Map_Leaf*)leaf->values)[index];
        if (new_leaf->values == NULL && new_leaf->keys == NULL) {
            *new_leaf = map_alloc_map_leafs(map);
        }
        leaf = new_leaf;
    }

    u8 index = cast(u8)((hash >> cast(Hash)((MAP_DEPTH-1) * 8)) & ~cast(Hash)0);
    Map_Leaf *new_leaf = (cast(V*)leaf->values)[index];
    if (new_leaf->values == NULL && new_leaf->keys == NULL) {
        map_leaf_init_key_values(map, new_leaf);
    }
    leaf = new_leaf;


}
