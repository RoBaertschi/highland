#include "highland.hpp"
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

enum class Log_Level {
    Debug,
    Info,
    Warning,
    Error,
    _Max,
};

void vlogf(Log_Level level, char const* format, va_list list) {
    b32 use_color = isatty(STDERR_FILENO);

    static String labels[cast(usize)Log_Level::_Max * 2] = {
        "\x1b[90m[DBG]\x1b[0m ",
        "\x1b[32m[INF]\x1b[0m ",
        "\x1b[33m[WRN]\x1b[0m ",
        "\x1b[31m[ERR]\x1b[0m ",

        // No color variants
        "[DBG] ",
        "[INF] ",
        "[WRN] ",
        "[ERR] ",
    };

    usize idx = (usize)level + (!use_color * (usize)Log_Level::_Max);
    auto s = labels[idx];
    write(STDERR_FILENO, s.ptr, s.len);

    vdprintf(STDERR_FILENO, format, list);
    dprintf(STDERR_FILENO, "\n");
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
