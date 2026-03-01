#include <errno.h>
#include <libudev.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm.h>
#include <string.h>
#include <fcntl.h>
#include <libseat.h>

#include "highland.hpp"

template <typename T>
internal void zero(T* ptr) {
    memset(ptr, 0, sizeof(*ptr));
}

struct Drm_Device {
    Drm_Device  *next;
    udev_device *device;
    String      path;
    b32         boot_vga;
};

// Mostly based on https://github.com/hyprwm/aquamarine/blob/7f9eb087703ec4acc6b288d02fa9ea3db803cd3d/src/backend/drm/DRM.cpp
internal Drm_Device *drm_find_gpus(udev *udev, Arena *arena) {
     Arena_Temp temp = temp_get_guard(&arena, 1);

     auto enumerate = udev_enumerate_new(udev);
     defer(udev_enumerate_unref(enumerate));

     if (!enumerate) {
         log_errorf("Could not enumerate GPU devices.");
         return NULL;
     }

    udev_enumerate_add_match_subsystem(enumerate, "drm");
#ifdef __linux__
    // https://github.com/wulf7/libudev-devd/issues/11
    udev_enumerate_add_match_property(enumerate, "DEVTYPE", "drm_minor");
#endif
    udev_enumerate_add_match_sysname(enumerate, DRM_PRIMARY_MINOR_NAME "[0-9]*");

     if (udev_enumerate_scan_devices(enumerate) < 0) {
         log_errorf("Could not scan for GPU devices.");
          return NULL;
     }

    Drm_Device *current_device = NULL;
    Drm_Device *start_device = NULL;
    udev_list_entry *entry = NULL;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerate)) {
        auto path = udev_list_entry_get_name(entry);
        auto device = udev_device_new_from_syspath(udev, path);
        if (!device) {
           log_warningf("Skipping device %s", path);
            continue;
         }

        // String seat = udev_device_get_property_value(device, "ID_SEAT");
        // if (!seat) {
        //     seat = "seat0";
        // }

        auto pci_device = udev_device_get_parent_with_subsystem_devtype(device, "pci", NULL);
        b32 boot_vga = false;
        if (pci_device) {
            auto id = udev_device_get_sysattr_value(pci_device, "boot_vga");
            boot_vga = String{id} == String{"1"};
        }

        log_infof("Found %s", path);

        if (!start_device) {
            current_device = start_device = arena_alloc<Drm_Device>(arena);
        } else {
            Drm_Device *new_device = arena_alloc<Drm_Device>(arena);
            current_device->next   = new_device;
            current_device = new_device;
        }

        current_device->device   = device;
        current_device->path     = arena_clone_string(arena, path);
        current_device->boot_vga = boot_vga;
    }

    return start_device;
}

internal void drm_do_stuff(Drm_Device &device) {
    Arena_Temp temp = temp_get_guard(NULL, 0);
    char const *path = udev_device_get_devnode(device.device);
    int fd = open(path, O_RDWR, O_CLOEXEC);
    if (fd < 0) {
        log_errorf("Could not open device(%s): %s", path, strerror(errno));
        return;
    }
    defer(close(fd));

    if (!drmIsKMS(fd)) {
        log_errorf("Device does not support kernel mode setting, which is requried for wayland.");
        return;
    }

    log_infof("Opened %s", path);
    auto res = drmModeGetResources(fd);
    defer(drmModeFreeResources(res));

    for (isize i = 0; i < res->count_connectors; i++) {
        auto connector = drmModeGetConnector(fd, res->connectors[i]);
        defer(drmModeFreeConnector(connector));

        switch (connector->connection) {
            case DRM_MODE_CONNECTED:
                log_infof("Connected", connector->count_modes, connector->modes->name);
                for (isize j = 0; j < connector->count_modes; j++) {
                    auto mode = connector->modes[j];
                    log_debugf("-> %s %d %d", mode.name, mode.vrefresh, mode.type);
                }
                break;
            case DRM_MODE_DISCONNECTED:      log_infof("Disconnected"); break;
            case DRM_MODE_UNKNOWNCONNECTION: log_infof("Unknown"); break;
        }
    }
}

// Returns errno on failiure and 0 on success
// int drm() {
//     Arena_Temp temp = temp_get_guard(NULL, 0);
//
//     int device_count = drmGetDevices2(0, NULL, 0);
//     if (device_count < 0) {
//         return -device_count;
//     }
//
//     if (device_count == 0) {
//         return 0;
//     }
//
//     auto devices = arena_alloc_slice<drmDevicePtr>(temp.arena, cast(isize)device_count);
//     device_count = drmGetDevices2(0, devices.ptr, devices.len);
//     defer(drmFreeDevices(devices.ptr, cast(int)devices.len));
//     if (device_count < 0) {
//         return -device_count;
//     }
//
//     auto deviceFds = arena_alloc_slice<drmDevicePtr>(temp.arena, cast(isize)devices.len);
//
//     isize i = 0;
//     for (drmDevicePtr device : slice(devices, 0, device_count)) {
//         int deviceFd = open(device->nodes[DRM_NODE_PRIMARY], O_RDWR | O_CLOEXEC);
//         if (deviceFd < 0) {
//             deviceFds[i] = errno;
//         } else {
//             deviceFds[i] = deviceFd;
//         }
//
//         i += 1;
//     }
//
//     return 0;
// }
