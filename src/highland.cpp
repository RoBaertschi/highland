#include <libudev.h>
#include <fmt/format.h>

#include "utils.cpp"
#include "arena.cpp"
#include "map.cpp"
#include "log.cpp"
#include "drm.cpp"

#define PROJECT_NAME "highland"

int main(int argc, char **argv) {
    auto udev = udev_new();
    Arena arena = {};

    log_init();

    auto devices = drm_find_gpus(udev, &arena);
    for(auto device = devices; device != NULL; device = device->next) {
        log_infof("Device in list {}", device->path);
        drm_do_stuff(*device);
    }

    if (argc != 1) {
        fmt::print("{} takes no arguments.\n", argv[0]);
        return 1;
    }

    fmt::print("This is project {}.\n", PROJECT_NAME);
    log_fatalf("Fuck");
    return 0;
}
