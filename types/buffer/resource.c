#include <assert.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/util/log.h>
#include <wlr/render/wlr_texture.h>
#include "types/wlr_buffer.h"

bool wlr_resource_is_buffer(struct wl_resource *resource) {
	return strcmp(wl_resource_get_class(resource), wl_buffer_interface.name) == 0;
}

/* struct wlr_buffer_resource_interface */
static struct wl_array buffer_resource_interfaces = {0};

void wlr_buffer_register_resource_interface(
		const struct wlr_buffer_resource_interface *iface) {
	assert(iface);
	assert(iface->is_instance);
	assert(iface->from_resource);

	const struct wlr_buffer_resource_interface **iface_ptr;
	wl_array_for_each(iface_ptr, &buffer_resource_interfaces) {
		if (*iface_ptr == iface) {
			wlr_log(WLR_DEBUG, "wlr_resource_buffer_interface %s has already"
					"been registered", iface->name);
			return;
		}
	}

	iface_ptr = wl_array_add(&buffer_resource_interfaces, sizeof(iface));
	*iface_ptr = iface;
}

static const struct wlr_buffer_resource_interface *get_buffer_resource_iface(
		struct wl_resource *resource) {
	struct wlr_buffer_resource_interface **iface_ptr;
	wl_array_for_each(iface_ptr, &buffer_resource_interfaces) {
		if ((*iface_ptr)->is_instance(resource)) {
			return *iface_ptr;
		}
	}

	return NULL;
}

struct wlr_buffer *wlr_buffer_from_resource(struct wl_resource *resource,
	struct wlr_renderer *renderer) {
	assert(resource && renderer && wlr_resource_is_buffer(resource));

	struct wlr_buffer *buffer;
	if (wl_shm_buffer_get(resource) != NULL) {
		struct wlr_shm_client_buffer *shm_client_buffer =
			shm_client_buffer_get_or_create(resource);
		if (shm_client_buffer == NULL) {
			wlr_log(WLR_ERROR, "Failed to create shm client buffer");
			return NULL;
		}
		buffer = wlr_buffer_lock(&shm_client_buffer->base);
	} else if (wlr_dmabuf_v1_resource_is_buffer(resource)) {
		struct wlr_dmabuf_v1_buffer *dmabuf =
			wlr_dmabuf_v1_buffer_from_buffer_resource(resource);
		buffer = wlr_buffer_lock(&dmabuf->base);
	} else if (wlr_drm_buffer_is_resource(resource)) {
		struct wlr_drm_buffer *drm_buffer =
			wlr_drm_buffer_from_resource(resource);
		buffer = wlr_buffer_lock(&drm_buffer->base);
	} else if ((buffer = wlr_buffer_from_wl_eglstream(renderer, resource, NULL))) {
		buffer = wlr_buffer_lock(buffer);
	} else {
		const struct wlr_buffer_resource_interface *iface =
				get_buffer_resource_iface(resource);
		if (!iface) {
			wlr_log(WLR_ERROR, "Unknown buffer type");
			return NULL;
		}

		struct wlr_buffer *custom_buffer = iface->from_resource(resource);
		if (!custom_buffer) {
			wlr_log(WLR_ERROR, "Failed to create %s buffer", iface->name);
			return NULL;
		}

		buffer = wlr_buffer_lock(custom_buffer);
	}

	return buffer;
}
