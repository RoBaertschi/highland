#include "highland.hpp"
#include "utils.cpp"
#include "log.cpp"

struct T {};

#define TEST_FUNC(name) void name(T* t)

internal TEST_FUNC(test_main);

internal void signal_test_handler(int signal) {
    log_fatalf("Received signal {}", strsignal(signal));
}

int main(char **argv, i32 argc) {
    sigaction sa = {};
    sa.sa_handler = signal_test_handler;
    sigemptyset(&sa.sa_mas&sa.sa_maskk);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);


    T t = {};
    test_main(&t);
}
