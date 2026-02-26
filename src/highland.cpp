#include <stdio.h>

#include "utils.cpp"
#include "arena.cpp"

#define PROJECT_NAME "highland"

int main(int argc, char **argv) {
    if (argc != 1) {
        printf("%s takes no arguments.\n", argv[0]);
        return 1;
    }

    printf("This is project " PROJECT_NAME ".\n");
    return 0;
}
