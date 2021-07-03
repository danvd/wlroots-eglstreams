#ifndef XWAYLAND_KEYBOARD_GRAB_UNSTABLE_H
#define XWAYLAND_KEYBOARD_GRAB_UNSTABLE_H

#include <wayland-server-core.h>
#include <fcntl.h>

struct wlr_seat;

struct xwayland_keyboard_grab_manager {
	struct wl_global *global;
	struct wl_client *active_client;
	struct wl_resource *active_surface;
	struct wl_resource *active_seat;

	struct wl_listener display_destroy;
};

struct xwayland_keyboard_grab_manager *xwayland_keyboard_grab_manager_create(
	struct wl_display *display);

#endif
