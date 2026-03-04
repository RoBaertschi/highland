#include <errno.h>
#include <libudev.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm.h>
#include <string.h>
#include <fcntl.h>
#include <libseat.h>
#include <gbm.h>

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

template <typename E>
struct Drm_Property_Name_Map_Value {
    String name;
    E      property;
};

template <typename E>
E drm_property_name_map_find(Drm_Property_Name_Map_Value<E> readonly *values, isize len, String name) {
    for (isize i = 0; i < len; i++) {
        auto mapping = values[i];
        if (mapping.name == name) {
            return mapping.property;
        }
    }

    return cast(E)0;
}

template <typename E>
String drm_property_name_map_find_by_name(Drm_Property_Name_Map_Value<E> readonly *values, isize len, E property) {
    for (isize i = 0; i < len; i++) {
        auto mapping = values[i];
        if (mapping.property == property) {
            return mapping.name;
        }
    }

    return "Invalid";
}

#define DRM_CONNECTOR_PROPERTY_LIST\
    X(CRTC_ID,      "CRTC_ID")     \
    X(DPMS,         "DPMS")        \
    X(EDID,         "EDID")        \
    X(LINK_STATUS,  "link-status") \
    X(CONTENT_TYPE, "content type")\
    X(SUBCONNECTOR, "subconnector")

enum Drm_Connector_Property {
    DRM_CONNECTOR_PROPERTY_INVALID,
#define X(name, ...) DRM_CONNECTOR_PROPERTY_##name,
    DRM_CONNECTOR_PROPERTY_LIST
#undef X
    DRM_CONNECTOR_PROPERTY__MAX,
};

global Drm_Property_Name_Map_Value<Drm_Connector_Property> readonly drm_connector_property_name_map[DRM_CONNECTOR_PROPERTY__MAX] {
#define X(name, s) { s, DRM_CONNECTOR_PROPERTY_##name },
    DRM_CONNECTOR_PROPERTY_LIST
#undef X
};

struct Drm_Connector {
    Drm_Connector       *next;
    drmModeConnectorPtr connector;
    u32                 properties[DRM_CONNECTOR_PROPERTY__MAX];
};

#define DRM_CRTC_PROPERTY_LIST       \
    X(ACTIVE,        "ACTIVE")       \
    X(MODE_ID,       "MODE_ID")      \
    X(OUT_FENCE_PTR, "OUT_FENCE_PTR")\
    X(VRR_ENABLED,   "VRR_ENABLED")  \
    X(GAMMA_LUT,     "GAMMA_LUT")    \
    X(DEGAMMA_LUT,   "DEGAMMA_LUT")  \
    X(CTM,           "CTM")

enum Drm_Crtc_Property {
    DRM_CRTC_PROPERTY_INVALID,
#define X(name, ...) DRM_CRTC_PROPERTY_##name,
    DRM_CRTC_PROPERTY_LIST
#undef X
    DRM_CRTC_PROPERTY__MAX,
};

global Drm_Property_Name_Map_Value<Drm_Crtc_Property> readonly drm_crtc_property_name_map[DRM_CRTC_PROPERTY__MAX] {
#define X(name, s) { s, DRM_CRTC_PROPERTY_##name },
    DRM_CRTC_PROPERTY_LIST
#undef X
};

struct Drm_Crtc {
    Drm_Crtc       *next;
    drmModeCrtcPtr crtc;
    u32            properties[DRM_CRTC_PROPERTY__MAX];
};

#define DRM_PLANE_PROPERTY_LIST          \
    X(FB_ID,           "FB_ID")          \
    X(CRTC_ID,         "CRTC_ID")        \
    X(SRC_X,           "SRC_X")          \
    X(SRC_Y,           "SRC_Y")          \
    X(SRC_W,           "SRC_W")          \
    X(SRC_H,           "SRC_H")          \
    X(IN_FORMATS,      "IN_FORMATS")     \
    X(TYPE,            "type")           \
    X(IN_FENCE_FD,     "IN_FENCE_FD")    \
    X(FB_DAMAGE_CLIPS, "FB_DAMAGE_CLIPS")\
    X(ROTATION,        "rotation")       \
    X(ZPOS,            "zpos")

enum Drm_Plane_Property {
    DRM_PLANE_PROPERTY_INVALID,
#define X(name, ...) DRM_PLANE_PROPERTY_##name,
    DRM_PLANE_PROPERTY_LIST
#undef X
    DRM_PLANE_PROPERTY__MAX,
};

global Drm_Property_Name_Map_Value<Drm_Plane_Property> readonly drm_plane_property_name_map[DRM_PLANE_PROPERTY__MAX] {
#define X(name, s) { s, DRM_PLANE_PROPERTY_##name },
    DRM_PLANE_PROPERTY_LIST
#undef X
};

struct Drm_Plane {
    Drm_Plane       *next;
    drmModePlanePtr plane;
    u32             properties[DRM_PLANE_PROPERTY__MAX];
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
            log_warnf("Skipping device {}", path);
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

        log_infof("Found {}", path);

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
        log_errorf("Could not open device({}): {}", path, strerror(errno));
        return;
    }
    defer(close(fd));

    if (!drmIsKMS(fd)) {
        log_errorf("Device does not support kernel mode setting, which is requried for wayland.");
        return;
    }

    int setMasterResult = drmSetMaster(fd);
    if (setMasterResult) {
        log_errorf("Could not set drm master: {}", strerror(errno));
        return;
    }

    b32 atomicSupported = true;
    int atomicSupportedResult = drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
    if (atomicSupportedResult) {
        log_infof("Atomic mode setting is not supported: {}", strerror(errno));
        atomicSupported = false;
    }

    int universalPlanesResult = drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    if (universalPlanesResult) {
        log_errorf("Universal planes not supported: {}", strerror(errno));
        return;
    }

    log_infof("Opened {}", path);
    auto res = drmModeGetResources(fd);
    defer(drmModeFreeResources(res));

    Drm_Connector *connected_connectors = NULL;
    Drm_Crtc      *crtcs                = NULL;

    for (isize i = 0; i < res->count_crtcs; i++) {
        auto crtc = drmModeGetCrtc(fd, res->crtcs[i]);
        defer(drmModeFreeCrtc(crtc));
        auto properties = drmModeObjectGetProperties(fd, res->crtcs[i], DRM_MODE_OBJECT_CRTC);
        defer(drmModeFreeObjectProperties(properties));
        if (properties) {
            auto element = arena_alloc<Drm_Crtc>(temp.arena);
            ll_insert_at_head(&crtcs, element);

            for (isize j = 0; j < properties->count_props; j++) {
                auto property = drmModeGetProperty(fd, properties->props[j]);
                defer(drmModeFreeProperty(property));
                log_infof("--> p: {} = {}", property->name, properties->prop_values[j]);

                Drm_Crtc_Property found_property = drm_property_name_map_find(drm_crtc_property_name_map, ARRAY_SIZE(drm_crtc_property_name_map), String{property->name});

                if (found_property) {
                    element->properties[cast(usize)found_property] = property->prop_id;
                }
            }

            if (!element->properties[DRM_CRTC_PROPERTY_ACTIVE] || !element->properties[DRM_CRTC_PROPERTY_MODE_ID]) {
                log_errorf(
                    "Missing crtcs properties. active_property={}, mode_id_property={}",
                    element->properties[DRM_CRTC_PROPERTY_ACTIVE],
                    element->properties[DRM_CRTC_PROPERTY_MODE_ID]);

                ll_remove_at_head(&crtcs);
            }
        }
    }

    for (isize i = 0; i < res->count_connectors; i++) {
        auto connector = drmModeGetConnector(fd, res->connectors[i]);
        defer({
            if (connector->connection != DRM_MODE_CONNECTED) {
                drmModeFreeConnector(connector);
            }
        });

        switch (connector->connection) {
            case DRM_MODE_CONNECTED: {
                Drm_Connector *element = arena_alloc<Drm_Connector>(temp.arena);
                element->connector = connector;
                ll_insert_at_head(&connected_connectors, element);
                log_infof("Connected");
                for (isize j = 0; j < connector->count_modes; j++) {
                    auto mode = connector->modes[j];
                    log_debugf("-> {} {} {} {:08b}", mode.name, mode.vrefresh, mode.type, mode.flags);

                    if (mode.type & DRM_MODE_TYPE_PREFERRED) {
                        log_debugf("--> Preferred");
                    }
                    if (mode.type & DRM_MODE_TYPE_USERDEF) {
                        log_debugf("--> Userdef");
                    }
                    if (mode.type & DRM_MODE_TYPE_DRIVER) {
                        log_debugf("--> Driver");
                    }

                    if (mode.flags & DRM_MODE_FLAG_PHSYNC) {
                        log_debugf("--> f: PHSYNC");
                    }
                    if (mode.flags & DRM_MODE_FLAG_NHSYNC) {
                        log_debugf("--> f: NHSYNC");
                    }
                    if (mode.flags & DRM_MODE_FLAG_PVSYNC) {
                        log_debugf("--> f: PVSYNC");
                    }
                    if (mode.flags & DRM_MODE_FLAG_NVSYNC) {
                        log_debugf("--> f: NVSYNC");
                    }
                }

                auto properties = drmModeObjectGetProperties(fd, res->connectors[i], DRM_MODE_OBJECT_CONNECTOR);
                defer(drmModeFreeObjectProperties(properties));
                if (properties) {
                    for (isize j = 0; j < properties->count_props; j++) {
                        auto property = drmModeGetProperty(fd, properties->props[j]);
                        defer(drmModeFreeProperty(property));
                        log_infof("--> p: {} = {}", property->name, properties->prop_values[j]);

                        Drm_Connector_Property found_property = drm_property_name_map_find(drm_connector_property_name_map, ARRAY_SIZE(drm_connector_property_name_map), String{property->name});

                        if (found_property) {
                            element->properties[found_property] = property->prop_id;
                        }
                    }
                }

                if (!element->properties[DRM_CONNECTOR_PROPERTY_CRTC_ID]) {
                    log_errorf("Missing CRTC_ID");
                    ll_remove_at_head(&connected_connectors);
                }
                break;
            }
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
