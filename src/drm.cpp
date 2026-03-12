#include <drm.h>
#include <drm/drm_fourcc.h>
#include <errno.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>
#include <fcntl.h>
#include <gbm.h>
#include <GLES3/gl3.h>
#include <libseat.h>
#include <libudev.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

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
    isize               preferred_mode;
    struct Drm_Crtc     *crtc;
    u32                 properties[DRM_CONNECTOR_PROPERTY__MAX];
    u64                 property_values[DRM_CONNECTOR_PROPERTY__MAX];
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
    Drm_Crtc         *next;
    u32              index;
    drmModeCrtcPtr   crtc;
    struct Drm_Plane *primary_plane;
    u32              properties[DRM_CRTC_PROPERTY__MAX];
    u64              property_values[DRM_CRTC_PROPERTY__MAX];
};

#define DRM_PLANE_PROPERTY_LIST          \
    X(FB_ID,           "FB_ID")          \
    X(CRTC_ID,         "CRTC_ID")        \
    X(SRC_X,           "SRC_X")          \
    X(SRC_Y,           "SRC_Y")          \
    X(SRC_W,           "SRC_W")          \
    X(SRC_H,           "SRC_H")          \
    X(CRTC_X,          "CRTC_X")         \
    X(CRTC_Y,          "CRTC_Y")         \
    X(CRTC_W,          "CRTC_W")         \
    X(CRTC_H,          "CRTC_H")         \
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
    u32             format;
    gbm_surface     *surface;
    EGLSurface      egl_surface;
    u32             width;
    u32             height;
    u32             properties[DRM_PLANE_PROPERTY__MAX];
    u64             property_values[DRM_PLANE_PROPERTY__MAX];
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

enum Drm_Egl_Extension {
    DRM_EGL_EXTENSION_KHR_PLATFORM_GBM,
    DRM_EGL_EXTENSION__MAX,
};

global String drm_egl_extensions[DRM_EGL_EXTENSION__MAX] = {
    "EGL_KHR_platform_gbm",
};

struct Drm_Egl_Context {
    EGLDisplay display;
    EGLConfig  config;
    EGLContext context;
};

internal Drm_Egl_Context drm_egl_init(gbm_device *device) {
    String extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    b8 found_extensions[DRM_EGL_EXTENSION__MAX] = {};

    isize start_extension = 0;
    for (isize i = 0; i < extensions.len; i++) {
        u8 c = extensions[i];
        if (c == ' ') {
            auto extension_name = slice(extensions, start_extension, i);
            start_extension = i+1;
            for (usize extension = 0; extension < DRM_EGL_EXTENSION__MAX; extension++) {
                if (drm_egl_extensions[extension] == extension_name) {
                    if (!found_extensions[extension]) {
                        found_extensions[extension] = true;
                    }
                }
            }
        }
    }

    usize missing_extensions = 0;
    for (usize extension = 0; extension < DRM_EGL_EXTENSION__MAX; extension++) {
        if (!found_extensions[extension]) {
            missing_extensions += 1;
            log_errorf("Missing egl extension: {}", drm_egl_extensions[extension]);
        }
    }
    if (missing_extensions > 0) {
        log_fatalf("Missing some required egl extensions");
    }

    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (eglGetPlatformDisplayEXT == NULL) {
        log_fatalf("Could not load eglGetPlatformDisplayEXT: {}", eglGetError());
    }

    auto display = eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR, device, NULL);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(display, &major, &minor)) {
        log_fatalf("Could not initialize egl: {}", eglGetError());
    }

    EGLint attribs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE,
    };
    EGLConfig config = {};
    EGLint config_count = 0;
    if (!eglChooseConfig(display, attribs, &config, 1, &config_count)) {
        log_fatalf("Could not choose egl config: {}", eglGetError());
    }

    if (config_count < 1) {
        log_fatalf("Could not find any matching egl config: {}", eglGetError());
    }

    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE,
    };

    auto context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attribs);
    if (!context) {
        log_fatalf("Could not create egl context: {}", eglGetError());
    }

    return Drm_Egl_Context {
        display,
        config,
        context,
    };
}

struct Drm_Gbm_Bo_Data {
    int fd;
    u32 fb_id;
};

internal void drm_gbm_bo_destroy_user_data(gbm_bo *bo, void *data) {
    (void)bo;
    auto bo_data = cast(Drm_Gbm_Bo_Data*)data;
    drmModeRmFB(bo_data->fd, bo_data->fb_id);
}

internal u32 drm_gbm_get_fb_id(Arena *arena, int fd, gbm_bo *bo, Drm_Plane *plane) {
    u32 fb_id = (u32)(uintptr)gbm_bo_get_user_data(bo);
    if (fb_id) {
        return fb_id;
    }

    u32 stride[4] = { gbm_bo_get_stride(bo) };
    u32 handle[4] = { gbm_bo_get_handle(bo).u32 };
    u32 offset[4] = { 0 };

    int result = drmModeAddFB2(
                fd,
                plane->width,
                plane->height,
                DRM_FORMAT_XRGB8888,
                handle,
                stride,
                offset,
                &fb_id,
                0);
    if (result < 0) {
        log_fatalf("Could not create new frame buffer: {}", strerror(-result));
    }

    auto data   = arena_alloc<Drm_Gbm_Bo_Data>(arena);
    data->fd    = fd;
    data->fb_id = fb_id;
    gbm_bo_set_user_data(bo, data, drm_gbm_bo_destroy_user_data);
    return fb_id;
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

    int set_master_result = drmSetMaster(fd);
    if (set_master_result) {
        log_errorf("Could not set drm master: {}", strerror(errno));
        return;
    }

    int atomic_supported_result = drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
    if (atomic_supported_result) {
        log_errorf("Atomic mode setting is not supported, which is currently requried: {}", strerror(errno));
        return;
    }

    int universal_planes_result = drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    if (universal_planes_result) {
        log_errorf("Universal planes not supported: {}", strerror(errno));
        return;
    }

    log_infof("Opened {}", path);
    auto res = drmModeGetResources(fd);
    defer(drmModeFreeResources(res));

    auto plane_res = drmModeGetPlaneResources(fd);
    defer(drmModeFreePlaneResources(plane_res));

    Drm_Connector *connected_connectors = NULL;
    Drm_Crtc      *crtcs                = NULL;
    Drm_Plane     *planes               = NULL;

    for (isize i = 0; i < res->count_crtcs; i++) {
        auto properties = drmModeObjectGetProperties(fd, res->crtcs[i], DRM_MODE_OBJECT_CRTC);
        defer(drmModeFreeObjectProperties(properties));

        if (!properties) {
            continue;
        }

        auto crtc    = drmModeGetCrtc(fd, res->crtcs[i]);
        auto element = arena_alloc<Drm_Crtc>(temp.arena);
        ll_insert_at_head(&crtcs, element);
        element->crtc  = crtc;
        element->index = i;

        for (isize j = 0; j < properties->count_props; j++) {
            auto property = drmModeGetProperty(fd, properties->props[j]);
            defer(drmModeFreeProperty(property));
            log_infof("--> p: {} = {}", property->name, properties->prop_values[j]);

            Drm_Crtc_Property found_property = drm_property_name_map_find(drm_crtc_property_name_map, ARRAY_SIZE(drm_crtc_property_name_map), String{property->name});

            if (found_property) {
                element->properties[found_property] = property->prop_id;
                element->property_values[found_property] = properties->prop_values[j];
            }
        }

        if (!element->properties[DRM_CRTC_PROPERTY_ACTIVE] || !element->properties[DRM_CRTC_PROPERTY_MODE_ID]) {
            log_errorf(
                "Missing crtcs properties. active_property={}, mode_id_property={}",
                element->properties[DRM_CRTC_PROPERTY_ACTIVE],
                element->properties[DRM_CRTC_PROPERTY_MODE_ID]);

            auto drm_crtc = ll_remove_at_head(&crtcs);
            drmModeFreeCrtc(drm_crtc->crtc);
        }
    }

    Drm_Crtc *assigned_to_connector_crtcs = NULL;

    for (isize i = 0; i < res->count_connectors; i++) {
        auto connector = drmModeGetConnector(fd, res->connectors[i]);
        defer({
            if (connector->connection != DRM_MODE_CONNECTED) {
                drmModeFreeConnector(connector);
            }
        });

        switch (connector->connection) {
        case DRM_MODE_CONNECTED: {
            auto properties = drmModeObjectGetProperties(fd, res->connectors[i], DRM_MODE_OBJECT_CONNECTOR);
            defer(drmModeFreeObjectProperties(properties));

            if (!properties) {
                continue;
            }

            u32 possible_crtcs = 0;
            for (isize j = 0; j < connector->count_encoders; j++) {
                auto encoder = drmModeGetEncoder(fd, connector->encoders[j]);
                defer (drmModeFreeEncoder(encoder));

                possible_crtcs |= encoder->possible_crtcs;
            }

            if (!possible_crtcs) {
                log_warnf("No compatible crtcs for connector.");
                continue;
            }

            Drm_Crtc *compatible_crtc = NULL;

            for (Drm_Crtc *crtc = crtcs, *previous_crtc = NULL, *next_crtc = NULL; crtc;
                    previous_crtc = crtc, crtc = next_crtc) {

                next_crtc = crtc->next;

                if ((1 << crtc->index) & possible_crtcs) {
                    possible_crtcs &= ~(1 << crtc->index);
                    if (previous_crtc) {
                        ASSERT(ll_remove_next(previous_crtc) == crtc);
                    } else {
                        ASSERT(ll_remove_at_head(&crtcs) == crtc);
                    }
                    ll_insert_at_head(&assigned_to_connector_crtcs, crtc);
                    compatible_crtc = crtc;
                    crtc = previous_crtc;
                }
            }

            if (!compatible_crtc) {
                log_warnf("No compatible crtcs for connector.");
                continue;
            }

            Drm_Connector *element = arena_alloc<Drm_Connector>(temp.arena);
            element->connector = connector;
            element->crtc      = compatible_crtc;
            ll_insert_at_head(&connected_connectors, element);
            log_infof("Connected");

            isize preferred_mode = -1;
            isize largest_idx    = -1;
            u16 largest_hdisplay = 0, largest_vdisplay = 0;
            u32 largest_vrefresh = 0;

            for (isize j = 0; j < connector->count_modes; j++) {
                auto mode = connector->modes[j];
                log_debugf("-> {} {} {} {:08b}", mode.name, mode.vrefresh, mode.type, mode.flags);

                if (mode.type & DRM_MODE_TYPE_PREFERRED) {
                    preferred_mode = j;
                    log_debugf("--> Preferred");
                    break;
                }

                if (mode.hdisplay == largest_hdisplay && mode.vdisplay == largest_vdisplay) {
                    if (mode.vrefresh > largest_vrefresh) {
                        largest_vrefresh = mode.vrefresh;
                        largest_idx      = j;
                    }
                } else if (mode.hdisplay >= largest_hdisplay && mode.vdisplay >= largest_vdisplay) {
                    largest_hdisplay = mode.hdisplay;
                    largest_vdisplay = mode.vdisplay;
                    largest_vrefresh = mode.vrefresh;
                    largest_idx = j;
                }
            }

            if (preferred_mode == -1) {
                preferred_mode = largest_idx;
            }

            if (preferred_mode == -1) {
                log_warnf("Could not find a mode for the connector.");
                goto connector_err;
            }

            element->preferred_mode = preferred_mode;

            for (isize j = 0; j < properties->count_props; j++) {
                auto property = drmModeGetProperty(fd, properties->props[j]);
                defer(drmModeFreeProperty(property));
                log_infof("--> p: {} = {}", property->name, properties->prop_values[j]);

                Drm_Connector_Property found_property = drm_property_name_map_find(drm_connector_property_name_map, ARRAY_SIZE(drm_connector_property_name_map), String{property->name});

                if (found_property) {
                    element->properties[found_property] = property->prop_id;
                    element->property_values[found_property] = properties->prop_values[j];
                    log_infof("---> p found: {} = {}", property->name, properties->prop_values[j]);
                }
            }

            if (element->properties[DRM_CONNECTOR_PROPERTY_CRTC_ID]) {
                break;
            }

            log_errorf("Missing CRTC_ID");

connector_err:
            auto drm_connector = ll_remove_at_head(&connected_connectors);
            drmModeFreeConnector(drm_connector->connector);
            break;
        }
        case DRM_MODE_DISCONNECTED:      log_infof("Disconnected"); break;
        case DRM_MODE_UNKNOWNCONNECTION: log_infof("Unknown"); break;
        }
    }

    log_debugf("Freeing unused crtcs.");
    for (auto crtc = crtcs; crtc; crtc = crtc->next) {
        drmModeFreeCrtc(crtc->crtc);
    }
    crtcs = NULL;

    Drm_Crtc *finished_crtcs = NULL;

    log_infof("Planes:");

    for (isize i = 0; i < plane_res->count_planes; i++) {
        auto plane = drmModeGetPlane(fd, plane_res->planes[i]);
        auto properties = drmModeObjectGetProperties(fd, plane_res->planes[i], DRM_MODE_OBJECT_PLANE);
        defer(drmModeFreeObjectProperties(properties));

        if (!properties) {
            continue;
        }

        auto element   = arena_alloc<Drm_Plane>(temp.arena);
        element->plane = plane;
        ll_insert_at_head(&planes, element);

        for (isize j = 0; j < properties->count_props; j++) {
            auto property = drmModeGetProperty(fd, properties->props[j]);
            defer(drmModeFreeProperty(property));

            Drm_Plane_Property found_property = drm_property_name_map_find(drm_plane_property_name_map, ARRAY_SIZE(drm_plane_property_name_map), String{property->name});
            log_infof("---> p found: {} = {}", property->name, properties->prop_values[j]);

            if (found_property) {
                element->properties[found_property] = property->prop_id;
                element->property_values[found_property] = properties->prop_values[j];
                log_infof("---> p found: {} = {}", property->name, properties->prop_values[j]);
            }
        }

        if (element->properties[DRM_PLANE_PROPERTY_TYPE]
                && element->properties[DRM_PLANE_PROPERTY_FB_ID]
                && element->properties[DRM_PLANE_PROPERTY_CRTC_ID]) {

            if (element->property_values[DRM_PLANE_PROPERTY_TYPE] != DRM_PLANE_TYPE_PRIMARY) {
                log_errorf("Plane is not primary.");
                goto plane_err;
            }

            if (false) {
                if (element->properties[DRM_PLANE_PROPERTY_IN_FORMATS]) {
                    auto blob = drmModeGetPropertyBlob(fd, element->property_values[DRM_PLANE_PROPERTY_IN_FORMATS]);
                    if (blob->length < cast(u32)sizeof(drm_format_modifier_blob)) {
                        log_errorf("Plane IN_FORMATS blob not enough space for the drm_format_modifier_blob.");
                        goto plane_err;
                    }

                    auto format_modifier = cast(drm_format_modifier_blob*)blob->data;

                    assert(false, "TODO: Implement handling of IN_FORMATS for planes");
                }
            } else {
                for (isize i = 0; i < plane->count_formats; i++) {
                    auto format = plane->formats[i];
                    if (format == DRM_FORMAT_XRGB8888) {
                        element->format = format;
                    }
                }

                if (!element->format) {
                    log_errorf("Could not find format for plane.");
                    goto plane_err;
                }
            }

            auto crtc = ll_remove_at_head(&assigned_to_connector_crtcs);
            ll_insert_at_head(&finished_crtcs, crtc);
            crtc->primary_plane = element;
            continue;
        }

        log_errorf("Missing TYPE={}, FB_ID={}, CRTC_ID={} or IN_FORMATS={}", element->properties[DRM_PLANE_PROPERTY_TYPE], element->properties[DRM_PLANE_PROPERTY_FB_ID], element->properties[DRM_PLANE_PROPERTY_CRTC_ID], element->properties[DRM_PLANE_PROPERTY_IN_FORMATS]);

plane_err:
        auto drm_plane = ll_remove_at_head(&planes);
        drmModeFreePlane(drm_plane->plane);
    }

    auto gbm = gbm_create_device(fd);
    if (!gbm) {
        log_fatalf("Could not create gbm device: {}", strerror(errno));
    }
    defer(gbm_device_destroy(gbm));

    auto egl_context = drm_egl_init(gbm);

    for (auto connector = connected_connectors; connector; connector = connector->next) {
        auto crtc      = connector->crtc;
        auto plane     = crtc->primary_plane;
        auto mode      = connector->connector->modes[connector->preferred_mode];
        plane->width   = cast(u32)mode.hdisplay;
        plane->height  = cast(u32)mode.vdisplay;
        plane->surface = gbm_surface_create(gbm, plane->width, plane->height, plane->format, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

        if (!plane->surface) {
            log_fatalf("Could not create gbm_surface: {}", strerror(errno));
        }

        plane->egl_surface = eglCreateWindowSurface(
               egl_context.display,
               egl_context.config,
               (EGLNativeWindowType)plane->surface,
               NULL);

        if (!plane->egl_surface) {
            log_fatalf("Could not create egl surface from gbm surface: {:x}", eglGetError());
        }

        if (!eglMakeCurrent(egl_context.display, plane->egl_surface, plane->egl_surface, egl_context.context)) {
            log_fatalf("Could not make egl context current: {:x}", eglGetError());
        }

        glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        eglSwapBuffers(egl_context.display, plane->egl_surface);
    }

    // do the initial atomic commit
    auto commit = drmModeAtomicAlloc();
    if (!commit) {
        log_errorf("Could not allocate atomic commit");
        return;
    }
    defer (drmModeAtomicFree(commit));

    for (auto connector = connected_connectors; connector; connector = connector->next) {
        auto mode      = connector->connector->modes[connector->preferred_mode];
        u32  mode_blob = 0;
        int success    = drmModeCreatePropertyBlob(fd, &mode, sizeof(mode), &mode_blob);
        if (0 < success) {
            log_errorf("Could not create property blob: {}", strerror(success));
            continue;
        }
        auto crtc     = connector->crtc;
        auto crtc_id  = crtc->crtc->crtc_id;
        auto plane    = crtc->primary_plane;
        auto plane_id = plane->plane->plane_id;

        auto bo = gbm_surface_lock_front_buffer(plane->surface);
        auto fb_id = drm_gbm_get_fb_id(temp.arena, fd, bo, plane);

        drmModeAtomicAddProperty(commit, connector->connector->connector_id, connector->properties[DRM_CONNECTOR_PROPERTY_CRTC_ID], cast(u64)crtc_id);

        drmModeAtomicAddProperty(commit, crtc_id, connector->crtc->properties[DRM_CRTC_PROPERTY_MODE_ID], cast(u64)mode_blob);
        drmModeAtomicAddProperty(commit, crtc_id, connector->crtc->properties[DRM_CRTC_PROPERTY_ACTIVE],  cast(u64)1);

        drmModeAtomicAddProperty(commit, plane_id, plane->properties[DRM_PLANE_PROPERTY_FB_ID],   fb_id);
        drmModeAtomicAddProperty(commit, plane_id, plane->properties[DRM_PLANE_PROPERTY_CRTC_ID], crtc_id);
        drmModeAtomicAddProperty(commit, plane_id, plane->properties[DRM_PLANE_PROPERTY_SRC_X],   0);
        drmModeAtomicAddProperty(commit, plane_id, plane->properties[DRM_PLANE_PROPERTY_SRC_Y],   0);
        drmModeAtomicAddProperty(commit, plane_id, plane->properties[DRM_PLANE_PROPERTY_SRC_W],   plane->width << 16);
        drmModeAtomicAddProperty(commit, plane_id, plane->properties[DRM_PLANE_PROPERTY_SRC_H],   plane->height << 16);
        drmModeAtomicAddProperty(commit, plane_id, plane->properties[DRM_PLANE_PROPERTY_CRTC_X],  0);
        drmModeAtomicAddProperty(commit, plane_id, plane->properties[DRM_PLANE_PROPERTY_CRTC_Y],  0);
        drmModeAtomicAddProperty(commit, plane_id, plane->properties[DRM_PLANE_PROPERTY_CRTC_W],  plane->width);
        drmModeAtomicAddProperty(commit, plane_id, plane->properties[DRM_PLANE_PROPERTY_CRTC_H],  plane->height);

        log_debugf("Commit ready");
    }

    int drm_commit_test_result = drmModeAtomicCommit(fd, commit, DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
    if (drm_commit_test_result < 0) {
        log_fatalf("Atomic test commit failed: {}", strerror(-drm_commit_test_result));
    }

    log_infof("Test commit was successfull");
}

internal void drm_restore(int fd, Drm_Connector *connectors) {
    for (auto connector = connectors; connector; connector = connector->next) {
        auto crtc = connector->crtc;
        drmModeSetCrtc(fd, crtc->crtc->crtc_id, crtc->crtc->buffer_id, crtc->crtc->x, crtc->crtc->y, &connector->connector->connector_id, 1, &crtc->crtc->mode);
    }

    drmDropMaster(fd);
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
