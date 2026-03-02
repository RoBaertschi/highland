#include "highland.hpp"
#include <stdlib.h>
#include <unistd.h>

#include <fmt/format.h>


// Gonna try some gross shit
struct Fd_Output_Iterator;
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
    write(iter.fd, iter.buffer.ptr, min(iter.buffer.len, iter.i));
    iter.i = 0;
    slice_zero(iter.buffer);
}

Fd_Output_Iterator& fd_output_iterator_add_char(Fd_Output_Iterator &iter, char c) {
    if (iter.i >= iter.buffer.len) {
        fd_output_iterator_flush(iter);
    }
    iter.buffer[iter.i] = c;
    iter.i              += 1;

    return iter;
}

enum class Log_Level {
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
    _Max,
};

internal Arena log_arena;
internal Fd_Output_Iterator log_output;

internal void log_init() {
    log_output.buffer = arena_alloc_slice<char>(&log_arena, 1024);
    log_output.fd     = STDERR_FILENO;
    log_output.i      = 0;
    slice_zero(log_output.buffer);
}

void vlogf(Log_Level level, String format, fmt::format_args args) {

    function_static b32 use_color = 2;
    if (use_color == 2) {
        use_color = isatty(STDERR_FILENO);
    }

    function_static String labels[cast(usize)Log_Level::_Max * 2] = {
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
    log_output = fmt::format_to(log_output, "{}", s);

    log_output = fmt::vformat_to(log_output, format_as(format), args);

    *log_output++ = '\n';
    fd_output_iterator_flush(log_output);
    // syncfs(log_output.fd);

    if (level == Log_Level::Fatal) {
        exit(1);
    }
}


template<typename ...Args>
void logf(Log_Level level, String format, Args ...args) {
    auto format_args = fmt::make_format_args(args...);
    vlogf(level, format, format_args);
}

template<typename ...Args>
void log_debugf(String format, Args&& ...args) {
    auto format_args = fmt::make_format_args(args...);

    vlogf(Log_Level::Debug, format, format_args);
}

template<typename ...Args>
void log_infof(String format, Args&& ...args) {
    auto format_args = fmt::make_format_args(args...);

    vlogf(Log_Level::Info, format, format_args);
}

template<typename ...Args>
void log_warnf(String format, Args&& ...args) {
    auto format_args = fmt::make_format_args(args...);

    vlogf(Log_Level::Warning, format, format_args);
}

template<typename ...Args>
void log_errorf(String format, Args&& ...args) {
    auto format_args = fmt::make_format_args(args...);

    vlogf(Log_Level::Error, format, format_args);
}

template<typename ...Args>
void log_fatalf(String format, Args&& ...args) {
    auto format_args = fmt::make_format_args(args...);

    vlogf(Log_Level::Fatal, format, format_args);
}
