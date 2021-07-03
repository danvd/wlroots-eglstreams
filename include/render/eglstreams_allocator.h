
#ifndef RENDER_EGLSTREAM_ALLOCATOR_H
#define RENDER_EGLSTREAM_ALLOCATOR_H

#include <wlr/types/wlr_buffer.h>
#include "render/allocator/allocator.h"
#include "wlr/render/egl.h"


struct wlr_eglstreams_allocator;

struct wlr_eglstream_plane {
	struct wlr_eglstream stream;
	uint32_t id;
	struct wl_list link; // wlr_eglstreams_allocator.planes
	uint32_t locks;
	struct wlr_eglstreams_allocator *alloc;
	int width;
	int height;
};

struct wlr_eglstream_buffer {
	struct wlr_buffer base;
	struct wlr_eglstream_plane *plane;
};

struct wlr_eglstreams_allocator {
	struct wlr_allocator base;

	struct wlr_drm_backend *drm;
	struct wlr_egl *egl;
	struct wl_list planes;
};

/**
 * Creates a new EGLStreams allocator from a DRM Renderer.
 */
struct wlr_allocator *
	wlr_eglstreams_allocator_create(struct wlr_backend *backend,
		struct wlr_renderer *renderer,
		uint32_t buffer_caps);

/**
 * Returns configured plane for given id if any.
 */
struct wlr_eglstream_plane *wlr_eglstream_plane_for_id(
		struct wlr_allocator *wlr_alloc, uint32_t plane_id);

void wlr_eglstream_dispose_planes(struct wlr_allocator *wlr_alloc);
void wlr_eglstream_recreate_planes(struct wlr_allocator *wlr_alloc);

#endif

