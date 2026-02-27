#include "highland.hpp"

#include <string.h>

#include <sys/mman.h>

struct Memory_Block {
    Memory_Block *next;
    void         *data;
    usize         commited;
    usize         reserved;
    usize         used;
    usize         base;
};

#define PAGE_SIZE 4096

Memory_Block *mem_block_new(isize commited, isize reserved) {
#ifdef MAP_ANONYMOUS
#   define HIGHLAND_ANONYMOUS MAP_ANONYMOUS
#elif defined(MAP_ANON)
#   define HIGHLAND_ANONYMOUS MAP_ANON
#endif

    commited += sizeof(Memory_Block);
    reserved += sizeof(Memory_Block);
    reserved = align_up(reserved, PAGE_SIZE);
    commited = min(commited, reserved);

    Memory_Block block = {};
    block.data = mmap(NULL, reserved, PROT_NONE, MAP_PRIVATE | HIGHLAND_ANONYMOUS, -1, 0);
    if (block.data != MAP_FAILED) {
        block.reserved = reserved;

        int result = mprotect(block.data, commited, PROT_READ | PROT_WRITE);
        // TODO(robin): better error handling
        assert(result == 0, "Could not remove protection on commited memory map.");
        block.commited = commited;
        block.used     = sizeof(Memory_Block);
    } else {
        block.data = NULL;
    }

    if (block.data == NULL) {
        return NULL;
    }

    *(cast(Memory_Block*)block.data) = block;
    return cast(Memory_Block*)block.data;
}

Memory_Block *mem_block_newx(isize commited, isize reserved) {
    auto block = mem_block_new(commited, reserved);
    ASSERT(block->data != NULL);
    ASSERT(block->reserved > 0);
    ASSERT(block->commited > 0);
    return block;
}

struct Arena {
    Memory_Block *block;
    Memory_Block *initial;
    usize         used;
    isize         default_commited;
    isize         default_reserved;
};

internal void memory_block_free(Memory_Block *block) {
    if (block == NULL) {
        return;
    }
    if (block->data == NULL || block->reserved == 0) {
        return;
    }

    munmap(block->data, block->reserved);
}

void *memory_block_alloc(Memory_Block *block, isize size, isize alignment) {
    if (block == NULL) {
        return NULL;
    }

    ASSERT(size >= 0);
    if (size == 0) {
        return NULL;
    }

    if (alignment <= 0) {
        alignment = 1;
    }
    ASSERT(is_power_of_two(alignment));

    uintptr base = cast(uintptr)block->data;
    ASSERT(base != 0);

    usize used = block->used;
    used = max(used, sizeof(Memory_Block));

    uintptr current = base + used;
    uintptr aligned = align_up(current, alignment);
    uintptr end     = aligned + cast(uintptr)size;

    usize needed = cast(usize)(end - base);
    if (needed > block->reserved) {
        return NULL;
    }

    if (needed > block->commited) {
        usize new_commited = align_up(needed, PAGE_SIZE);
        new_commited = min(new_commited, block->reserved);

        int result = mprotect(block->data, new_commited, PROT_READ | PROT_WRITE);
        assert(result == 0, "Could not commit memory.");
        block->commited = new_commited;
    }

    block->used = needed;
    return cast(void*)aligned;
}

void *arena_alloc_raw(Arena *arena, isize size, isize alignment) {
    if (arena == NULL) {
        return NULL;
    }

    ASSERT(size >= 0);
    if (size == 0) {
        return NULL;
    }

    if (alignment <= 0) {
        alignment = 1;
    }

    if (arena->block == NULL) {
        isize want = size + alignment;
        isize commited = align_up(max(arena->default_commited, want), PAGE_SIZE);
        isize reserved = align_up(max(arena->default_reserved, commited), PAGE_SIZE);

        arena->block = mem_block_newx(commited, reserved);
        arena->block->next = NULL;
        arena->block->base = 0;
        arena->initial = arena->block;
        arena->used = 0;
    }

    void *ptr = memory_block_alloc(arena->block, size, alignment);
    if (ptr == NULL) {
        isize want = size + alignment;

        isize commited = align_up(max(arena->default_commited, want), PAGE_SIZE);

        isize old_data = cast(isize)(arena->block->reserved - sizeof(Memory_Block));
        isize grow_data = old_data * 2;

        isize reserved = align_up(max(max(arena->default_reserved, grow_data), commited), PAGE_SIZE);

        Memory_Block *new_block = mem_block_newx(commited, reserved);
        new_block->base = arena->used;
        new_block->next = arena->block;
        arena->block = new_block;

        ptr = memory_block_alloc(arena->block, size, alignment);
    }

    if (ptr != NULL) {
        usize block_used = arena->block->used;
        block_used = max(block_used, sizeof(Memory_Block));
        arena->used = arena->block->base + (block_used - sizeof(Memory_Block));
    }
    return ptr;
}

void arena_reset_to(Arena *arena, isize to) {
    if (arena == NULL) {
        return;
    }
    ASSERT(to >= 0);

    if (arena->initial == NULL) {
        arena->block = NULL;
        arena->used = 0;
        return;
    }

    usize uto = cast(usize)to;

    Memory_Block *b = arena->block;
    while (b != NULL && b != arena->initial) {
        Memory_Block *next = b->next;
        memory_block_free(b);
        b = next;
    }

    arena->block = arena->initial;
    arena->block->next = NULL;
    arena->block->base = 0;

    usize old_used = arena->block->used;
    old_used = max(old_used, sizeof(Memory_Block));

    usize new_used = sizeof(Memory_Block) + uto;
    ASSERT(new_used <= old_used);

    if (old_used > new_used) {
        memset(cast(u8*)arena->block->data + new_used, 0, old_used - new_used);
    }

    arena->block->used = new_used;
    arena->used = uto;
}

template <typename T>
T* arena_alloc(Arena *arena) {
    return cast(T*)arena_alloc_raw(arena, sizeof(T), alignof(T));
}

template <typename T>
Slice<T> arena_alloc_slice(Arena *arena, isize len) {
    T *data = cast(T*)arena_alloc_raw(arena, sizeof(T) * len, alignof(T));
    return slice_from_raw(data, data ? len : 0);
}

String arena_clone_string(Arena *arena, Slice<u8> s) {
    auto copy = arena_alloc_slice<u8>(arena, s.len);
    if (copy.len > 0) {
        slice_copy(copy, s);
    }

    return String { copy.ptr, copy.len };
}

String arena_clone_string(Arena *arena, String s) {
    auto copy = arena_alloc_slice<u8>(arena, s.len);
    if (copy.len > 0) {
        Slice<u8 const> s2 = { s.ptr, s.len };
        slice_copy(copy, s2);
    }

    return String { copy.ptr, copy.len };
}

char const* arena_clone_cstring(Arena *arena, String s) {
    auto copy = arena_alloc_slice<u8>(arena, s.len + 1);
    if (copy.len > 0) {
        Slice<u8 const> s2 = { s.ptr, s.len };
        slice_copy(copy, s2);
        copy[copy.len-1] = 0;
    }
    return cast(char const*)copy.ptr;
}

struct Arena_Temp {
    Arena *arena;
    usize pos;

    ~Arena_Temp() {
        arena_reset_to(arena, pos);
    }
};

Arena_Temp arena_temp_guard(Arena *arena) {
    return Arena_Temp {
        arena,
        arena->used,
    };
}

#define TEMP_ARENA_COUNT 2

thread_local Arena temp_arenas[TEMP_ARENA_COUNT];

Arena *temp_arena_get(Arena **collisions, isize collisions_count) {
    for (isize i = 0; i < TEMP_ARENA_COUNT; i++) {
        b32 found = false;
        for (isize j = 0; j < collisions_count; j++) {
            if (collisions[j] == &temp_arenas[i]) {
                found = true;
                break;
            }
        }

        if (!found) {
            return &temp_arenas[i];
        }
    }

    assert(false, "Out of temp arenas.");
    return NULL;
}

Arena_Temp temp_get_guard(Arena **collisions, isize collisions_count) {
    Arena *arena = temp_arena_get(collisions, collisions_count);
    return Arena_Temp {
        arena,
        arena->used,
    };
}
