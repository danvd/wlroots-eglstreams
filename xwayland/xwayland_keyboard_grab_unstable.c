#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include "xwayland_keyboard_grab_unstable.h"
#include "xwayland-keyboard-grab-unstable-v1-protocol.h"
#include "wlr/xwayland.h"
#include "util/signal.h"
#include "wlr/util/log.h"

static const struct zwp_xwayland_keyboard_grab_manager_v1_interface
	xwayland_keyboard_grab_manager_implementation;
static const struct zwp_xwayland_keyboard_grab_v1_interface
	xwayland_keyboard_grab_implementation;

struct xwayland_keyboard_grab_manager *xwayland_keyboard_grab_manager = NULL;

static struct xwayland_keyboard_grab_manager *xwayland_keyboard_grab_manager_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
			&zwp_xwayland_keyboard_grab_manager_v1_interface,
			&xwayland_keyboard_grab_manager_implementation)
		|| wl_resource_instance_of(resource,
			&zwp_xwayland_keyboard_grab_v1_interface,
			&xwayland_keyboard_grab_implementation));
	return wl_resource_get_user_data(resource);
}

static void xwayland_keyboard_grab_manager_deactivate(
		struct xwayland_keyboard_grab_manager *manager) {
	if (manager->active_client == NULL) {
		return;
	}
	manager->active_client = NULL;
	manager->active_surface = NULL;
	manager->active_seat = NULL;
}


static void xwayland_keyboard_grab_manager_destroy(struct wl_client *client,
			struct wl_resource *resource) {
	struct xwayland_keyboard_grab_manager *manager =
		xwayland_keyboard_grab_manager_from_resource(resource);
	xwayland_keyboard_grab_manager_deactivate(manager);
	wl_resource_destroy(resource);
}

static void xwayland_keyboard_grab_destroy(struct wl_client *client,
			struct wl_resource *resource) {
	struct xwayland_keyboard_grab_manager *manager =
		xwayland_keyboard_grab_manager_from_resource(resource);
	if (manager->active_client != client) {
		return;
	}
	wl_resource_destroy(resource);
	xwayland_keyboard_grab_manager_deactivate(manager);
}

static void xwayland_keyboard_grab_resource_destroy(struct wl_resource *resource) {
	struct xwayland_keyboard_grab_manager *manager =
		xwayland_keyboard_grab_manager_from_resource(resource);
	xwayland_keyboard_grab_manager_deactivate(manager);
}

static const struct zwp_xwayland_keyboard_grab_v1_interface xwayland_keyboard_grab_implementation = {
	.destroy = xwayland_keyboard_grab_destroy
};

static void grab_keyboard(struct wl_client *client,
		struct wl_resource *resource,
		uint32_t id,
		struct wl_resource *surface,
		struct wl_resource *seat) {

	struct xwayland_keyboard_grab_manager *manager =
		xwayland_keyboard_grab_manager_from_resource(resource);

	struct wl_resource *wl_resource = wl_resource_create(client,
			&zwp_xwayland_keyboard_grab_v1_interface,
			wl_resource_get_version(resource), id);
	if (!wl_resource) {
		wl_client_post_no_memory(client);
	}

	wl_resource_set_implementation(wl_resource, &xwayland_keyboard_grab_implementation,
			manager, xwayland_keyboard_grab_resource_destroy);

	struct wlr_surface* wlr_surface = wlr_surface_from_resource(surface);
	if (!wlr_surface_is_xwayland_surface(wlr_surface)) {
		wlr_log(WLR_ERROR,
			"Xwayland keyboard grab manager got request from unknown surface: %p",
			surface);
		xwayland_keyboard_grab_manager_deactivate(manager);
		return;
	}

	struct wlr_xwayland_surface *xwayland_surface = wlr_xwayland_surface_from_wlr_surface(wlr_surface);
	if(!xwayland_surface->fullscreen) {
		wlr_log(WLR_ERROR, "Xwayland keyboard grab manager has refused keyboard grab "
			"request from non-fullscreen surface");
		xwayland_keyboard_grab_manager_deactivate(manager);
		return;
	}

	manager->active_client = client;
	manager->active_surface = surface;
	manager->active_seat = seat;
}

static const struct zwp_xwayland_keyboard_grab_manager_v1_interface
	xwayland_keyboard_grab_manager_implementation = {
		.grab_keyboard = grab_keyboard ,
		.destroy = xwayland_keyboard_grab_manager_destroy
};

static void xwayland_keyboard_grab_manager_resource_destroy(struct wl_resource *resource) {
	struct xwayland_keyboard_grab_manager *manager =
		xwayland_keyboard_grab_manager_from_resource(resource);
	struct wl_client *client = wl_resource_get_client(resource);
	if (manager->active_client == client) {
		xwayland_keyboard_grab_manager_deactivate(manager);
	}
}

static void xwayland_keyboard_grab_manager_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct xwayland_keyboard_grab_manager *manager = data;
	assert(wl_client && manager);

	struct wl_resource *wl_resource = wl_resource_create(wl_client,
		&zwp_xwayland_keyboard_grab_manager_v1_interface, version, id);
	if (wl_resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(wl_resource,
			&xwayland_keyboard_grab_manager_implementation, manager,
			xwayland_keyboard_grab_manager_resource_destroy);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct xwayland_keyboard_grab_manager *manager =
		wl_container_of(listener, manager, display_destroy);
	wl_list_remove(&manager->display_destroy.link);
	wl_global_destroy(manager->global);
	free(manager);
}

struct xwayland_keyboard_grab_manager *xwayland_keyboard_grab_manager_create(
		struct wl_display *display) {

	assert(!xwayland_keyboard_grab_manager);

	struct xwayland_keyboard_grab_manager *manager =
		calloc(1, sizeof(struct xwayland_keyboard_grab_manager));
	if (!manager) {
		return NULL;
	}

	manager->global = wl_global_create(display,
			&zwp_xwayland_keyboard_grab_manager_v1_interface,
			1, manager, xwayland_keyboard_grab_manager_bind);
	if (manager->global == NULL){
		free(manager);
		return NULL;
	}

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	xwayland_keyboard_grab_manager = manager;

	return manager;
}

