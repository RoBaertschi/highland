#include "fmt/base.h"
#include "fmt/format.h"
#include "highland.hpp"

#include <unistd.h>
#include <string.h>

#define ASSERT(expr) assert((expr), #expr)

[[noreturn]] internal void trap(void) {
    __builtin_trap();
}

struct String {
    u8 const *ptr;
    isize    len;

    String() = default;
    String(u8 const *ptr, isize len) : ptr(ptr), len(len) {};
    String(char const* str) : ptr(cast(u8 const *)str), len(strlen(str)) {}

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
Slice<T> slice_from_raw(T *ptr, isize len) {
    Slice<T> slice = {};
    slice.ptr = ptr;
    slice.len = len;
    return slice;
}

template <typename T>
Slice<T> slice(Slice<T> s, isize start, isize end) {
    ASSERT(0 <= start && start <= end && end <= s.len);
    Slice<T> slice = {};
    slice.len = end - start;
    if (slice.len > 0) {
        slice.ptr = s.ptr + start;
    }
    return slice;
}

template <typename T>
isize slice_copy(Slice<T> to, Slice<T> from) {
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
isize slice_copy(Slice<T> to, Slice<T const> from) {
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
void slice_zero(Slice<T> slice) {
    memset(slice.ptr, 0, slice.len * sizeof(T));
}
