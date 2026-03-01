#include "highland.hpp"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

#include <fmt/base.h>

struct Fd_Output_Iterator;

// Gonna try some gross shit
Fd_Output_Iterator& fd_output_iterator_add_char(Fd_Output_Iterator &iter, char c);

struct Fd_Output_Iterator {
    typedef ptrdiff_t difference_type;

    int         fd;
    isize       i;
    Slice<char> buffer;

    Fd_Output_Iterator& operator=(char c) {
        return fd_output_iterator_add_char(*this, c);
    }

    Fd_Output_Iterator& operator*() { return *this; }
    Fd_Output_Iterator& operator++() { return *this; }
    Fd_Output_Iterator& operator++(int) { return *this; }
};

void fd_output_iterator_flush(Fd_Output_Iterator &iter) {
    write(iter.fd, iter.buffer.ptr, iter.buffer.len);
    iter.i = 0;
}

Fd_Output_Iterator& fd_output_iterator_add_char(Fd_Output_Iterator &iter, char c) {
    if (iter.i >= iter.buffer.len) {
        fd_output_iterator_flush(iter);
    }
    iter.buffer[iter.i] = c;
    iter.i              += 1;

    return iter;
}

void test(Arena *arena, int fd) {
    Fd_Output_Iterator output = {};
    output.buffer = arena_alloc_slice<char>(arena, 512);
    output.fd     = fd;
    fmt::format_to(output, "Hi");
    fd_output_iterator_flush(output);
}

enum class Log_Level {
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
    _Max,
};

void vlogf(Log_Level level, char const* format, va_list list) {
    b32 use_color = isatty(STDERR_FILENO);

    static String labels[cast(usize)Log_Level::_Max * 2] = {
        "\x1b[90m[DBG]\x1b[0m ",
        "\x1b[32m[INF]\x1b[0m ",
        "\x1b[33m[WRN]\x1b[0m ",
        "\x1b[31m[ERR]\x1b[0m ",
        "\x1b[31m[FTL]\x1b[0m ",

        // No color variants
        "[DBG] ",
        "[INF] ",
        "[WRN] ",
        "[ERR] ",
        "[FTL] ",
    };

    usize idx = (usize)level + (!use_color * (usize)Log_Level::_Max);
    auto s = labels[idx];
    write(STDERR_FILENO, s.ptr, s.len);

    vdprintf(STDERR_FILENO, format, list);
    dprintf(STDERR_FILENO, "\n");

    if (level == Log_Level::Fatal) {
        exit(1);
    }
}

void logf(Log_Level level, char const* format, ...) {
    va_list a;
    va_start(a, format);
    defer(va_end(a));

    vlogf(level, format, a);
}

void log_debugf(char const* format, ...) {
    va_list a;
    va_start(a, format);
    defer(va_end(a));

    vlogf(Log_Level::Debug, format, a);
}

void log_infof(char const* format, ...) {
    va_list a;
    va_start(a, format);
    defer(va_end(a));

    vlogf(Log_Level::Info, format, a);
}

void log_warningf(char const* format, ...) {
    va_list a;
    va_start(a, format);
    defer(va_end(a));

    vlogf(Log_Level::Warning, format, a);
}

void log_errorf(char const* format, ...) {
    va_list a;
    va_start(a, format);
    defer(va_end(a));

    vlogf(Log_Level::Error, format, a);
}

void log_fatalf(char const* format, ...) {
    va_list a;
    va_start(a, format);
    defer(va_end(a));

    vlogf(Log_Level::Fatal, format, a);
}
