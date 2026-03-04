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

internal void fd_output_iterator_flush(Fd_Output_Iterator &iter) {
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
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    FATAL,
    _MAX,
};

internal Arena log_arena;
internal Fd_Output_Iterator log_output;

internal void log_init() {
    log_output.buffer = arena_alloc_slice<char>(&log_arena, 1024);
    log_output.fd     = STDERR_FILENO;
    log_output.i      = 0;
    slice_zero(log_output.buffer);
}

internal void vlogf(Log_Level level, String format, fmt::format_args args) {

    function_static b32 use_color = 2;
    if (use_color == 2) {
        use_color = isatty(STDERR_FILENO);
    }

    function_static String labels[cast(usize)Log_Level::_MAX * 2] = {
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

    usize idx = (usize)level + (!use_color * cast(usize)Log_Level::_MAX);
    auto s = labels[idx];
    log_output = fmt::format_to(log_output, "{}", s);

    log_output = fmt::vformat_to(log_output, format_as(format), args);

    *log_output++ = '\n';
    fd_output_iterator_flush(log_output);

    if (level == Log_Level::FATAL) {
        exit(1);
    }
}


template<typename ...Args>
internal void logf(Log_Level level, String format, Args ...args) {
    auto format_args = fmt::make_format_args(args...);
    vlogf(level, format, format_args);
}

template<typename ...Args>
internal void log_debugf(String format, Args&& ...args) {
    auto format_args = fmt::make_format_args(args...);

    vlogf(Log_Level::DEBUG, format, format_args);
}

template<typename ...Args>
internal void log_infof(String format, Args&& ...args) {
    auto format_args = fmt::make_format_args(args...);

    vlogf(Log_Level::INFO, format, format_args);
}

template<typename ...Args>
internal void log_warnf(String format, Args&& ...args) {
    auto format_args = fmt::make_format_args(args...);

    vlogf(Log_Level::WARNING, format, format_args);
}

template<typename ...Args>
internal void log_errorf(String format, Args&& ...args) {
    auto format_args = fmt::make_format_args(args...);

    vlogf(Log_Level::ERROR, format, format_args);
}

template<typename ...Args>
internal void log_fatalf(String format, Args&& ...args) {
    auto format_args = fmt::make_format_args(args...);

    vlogf(Log_Level::FATAL, format, format_args);
}
