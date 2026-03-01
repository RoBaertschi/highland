#include <libudev.h>
#include <stdio.h>

#include "utils.cpp"
#include "arena.cpp"
#include "log.cpp"
#include "drm.cpp"

#define PROJECT_NAME "highland"

int main(int argc, char **argv) {
    auto udev = udev_new();
    Arena arena = {};

    test(&arena, STDOUT_FILENO);

    auto devices = drm_find_gpus(udev, &arena);
    for(auto device = devices; device != NULL; device = device->next) {
        log_infof("Device in list %s", device->path);
        drm_do_stuff(*device);
    }

    if (argc != 1) {
        printf("%s takes no arguments.\n", argv[0]);
        return 1;
    }

    printf("This is project " PROJECT_NAME ".\n");
    log_fatalf("Fuck");
    return 0;
}
