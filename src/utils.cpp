#include "fmt/base.h"
#include "fmt/format.h"
#include "highland.hpp"

#include <unistd.h>
#include <string.h>

internal inline isize count_leading_zeros(u32 n) {
    if (!n) {
        return 32;
    }
    return cast(isize)__builtin_clz(n);
}

internal inline isize count_leading_zeros(u64 n) {
    if (!n) {
        return 64;
    }
    return cast(isize)__builtin_clzll(n);
}

internal inline isize count_ones(u32 n) {
    return cast(isize)__builtin_popcount(n);
}

#define ASSERT(expr) assert((expr), #expr)

[[noreturn]] internal void trap(void) {
    __builtin_trap();
}


struct String;

internal void assert(b32 expression, String error);

struct String {
    u8 const *ptr;
    isize    len;

    String() = default;
    String(u8 const *ptr, isize len) : ptr(ptr), len(len) {};
    String(char const* str) : ptr(cast(u8 const *)str), len(strlen(str)) {}

    u8 operator[](isize index) const {
        ASSERT(0 <= index && index < len);
        return ptr[index];
    }

    bool operator ==(String const& other) {
        if (len != other.len) {
            return false;
        }

        for (isize i = 0; i < len; i++) {
            if (other.ptr[i] != ptr[i]) {
                return false;
            }
        }

        return true;
    }
};

fmt::string_view format_as(String s) {
    return fmt::string_view{ cast(char*)s.ptr, cast(usize)s.len };
}

internal void string_print(String s, int fd);

internal void assert(b32 expression, String error) {
    if (likely(expression)) {
        return;
    }

    string_print("ASSERTION FAILED: ", STDERR_FILENO);
    string_print(error, STDERR_FILENO);
    string_print("\n", STDERR_FILENO);
    trap();
}

internal b32 is_power_of_two(isize value) {
    return value > 0 && (value & (value - 1)) == 0;
}

template <typename T>
internal T min(T a, T b) {
    return a < b ? a : b;
}

template <typename T>
internal T max(T a, T b) {
    return a > b ? a : b;
}

template <typename T>
internal T align_up(T value, usize alignment) {
    ASSERT(is_power_of_two(cast(isize)alignment));
    T bitmask = cast(T)(alignment - 1);
    return (value + bitmask) & ~bitmask;
}

internal void string_print(String s, int fd) {
    ASSERT(s.len >= 0);
    write(fd, s.ptr, s.len);
}

template <typename T>
struct Slice {
    T     *ptr;
    isize len;

    T& operator[](isize index) {
        ASSERT(0 <= index && index < len);
        return ptr[index];
    }

    T const operator[](isize index) const {
        ASSERT(0 <= index && index < len);
        return ptr[index];
    }

    T *begin() noexcept {
        return ptr;
    }

    T *end() noexcept {
        return ptr + len;
    }
};

template <typename T>
internal Slice<T> slice_from_raw(T *ptr, isize len) {
    Slice<T> slice = {};
    slice.ptr = ptr;
    slice.len = len;
    return slice;
}

template <typename T>
internal Slice<T> slice(Slice<T> s, isize start, isize end) {
    ASSERT(0 <= start && start <= end && end <= s.len);
    Slice<T> slice = {};
    slice.len = end - start;
    if (slice.len > 0) {
        slice.ptr = s.ptr + start;
    }
    return slice;
}

internal String slice(String s, isize start, isize end) {
    ASSERT(0 <= start && start <= end && end <= s.len);
    String slice = {};
    slice.len = end - start;
    if (slice.len > 0) {
        slice.ptr = s.ptr + start;
    }
    return slice;
}

template <typename T>
internal isize slice_copy(Slice<T> to, Slice<T> from) {
    isize n = to.len;
    if (to.len > from.len) {
        n = from.len;
    }
    if (n > 0) {
        memcpy(to.ptr, from.ptr, n);
    }
    return n;
}

template <typename T>
internal isize slice_copy(Slice<T> to, Slice<T const> from) {
    isize n = to.len;
    if (to.len > from.len) {
        n = from.len;
    }
    if (n > 0) {
        memcpy(to.ptr, from.ptr, n);
    }
    return n;
}

template <typename T>
internal void slice_zero(Slice<T> slice) {
    memset(slice.ptr, 0, slice.len * sizeof(T));
}

template <typename T>
internal void ll_insert_at_head(T** head, T* new_element) {
    if (*head == NULL) {
        *head = new_element;
    } else {
        new_element->next = *head;
        *head = new_element;
    }
}

template <typename T>
internal void ll_insert_after(T* where, T* new_element) {
    new_element->next = where;
    where->next       = new_element;
}

template <typename T>
internal T* ll_remove_next(T* prev) {
    T* saved = prev->next;
    prev->next = prev->next->next;
    return saved;
}

template <typename T>
internal T* ll_remove_at_head(T** head) {
    if (*head == NULL) {
        return NULL;
    }

    T* saved = *head;
    *head = saved->next;
    return saved;
}

// An dynamic array with static backing data
template <typename T>
struct Alloc_Array {
    isize    len;
    Slice<T> data;

    T& operator[](isize index) {
        ASSERT(0 <= index && index < len);
        return *(&data.ptr[index]);
    }

    T const operator[](isize index) const {
        ASSERT(0 <= index && index < len);
        return data.ptr[index];
    }

    T *begin() noexcept {
        return data.ptr;
    }

    T *end() noexcept {
        return data.ptr + len;
    }
};

template <typename T>
internal void alloc_array_init(Alloc_Array<T> *array, Slice<T> backing) {
    *array = {
        0,
        backing,
    };
}

template <typename T>
internal Alloc_Array<T> alloc_array_create(Slice<T> backing) {
    return {
        0,
        backing,
    };
}

template <typename T>
internal void alloc_array_push(Alloc_Array<T>& array, T item) {
    if (likely(array.data.len > array.len)) {
        array.data[array.len] = item;
        array.len += 1;
    } else {
        // TODO(robin): should this fail or just be a no-op
        assert(false, "Alloc Array out of space.");
    }
}

template <typename T>
internal void alloc_array_unordered_remove(Alloc_Array<T>& array, isize index) {
    ASSERT(0 <= index && index < array.len);
    if (array.len > 1) {
        array[index] = array[array.len - 1];
    }
    array.len -= 1;
}

template <typename T>
internal Slice<T> slice(Alloc_Array<T>& array) {
    return slice(array.data, 0, array.len);
}

// robin: algorithms


// param bool sort_function(void *user_data, T lhs, T rhs); Should return true if lhs is smaller than rhs (<)
template <typename T, typename F>
internal void quick_sort(Slice<T> items, F sort_function, void *user_data) {
    T temp = {};

    if (items.len <= 1) {
        // already sorted
        return;
    }

    isize i = -1, j = 0;
    isize pivot = items.len - 1;
    for (; j < pivot; j++) {
        if (sort_function(user_data, items[j], items[pivot])) {
            i += 1;

            // Use j + 1
            temp = items[i];
            items[i] = items[j];
            items[j] = temp;
        }
    }

    i += 1;
    temp = items[i];
    items[i] = items[pivot];
    items[pivot] = temp;

    quick_sort(slice(items, 0, i), sort_function, user_data);
    quick_sort(slice(items, i + 1, items.len), sort_function, user_data);
    return;
}