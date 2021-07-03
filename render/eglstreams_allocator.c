
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include <backend/drm/drm.h>
#include <wlr/backend/interface.h>
#include <wlr/render/gles2.h>
#include "render/eglstreams_allocator.h"
#include "render/wlr_renderer.h"
#include "wlr/backend/multi.h"

static const struct wlr_buffer_impl buffer_impl;

static struct wlr_eglstream_buffer *get_eglstresms_buffer_from_buffer(
		struct wlr_buffer *buffer) {
	assert(buffer->impl == &buffer_impl);
	return (struct wlr_eglstream_buffer *)buffer;
}

static struct wlr_eglstream_buffer *create_buffer(struct wlr_eglstreams_allocator *alloc,
		struct wlr_eglstream_plane *plane) {

	struct wlr_eglstream_buffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		return NULL;
	}
	wlr_buffer_init(&buffer->base, &buffer_impl, plane->width, plane->height);

	buffer->base.egl_stream = &plane->stream;
	buffer->plane = plane;
	plane->locks++;

	wlr_log(WLR_DEBUG, "Allocated %dx%d EGLStreams buffer",
		buffer->base.width, buffer->base.height);

	return buffer;
}

static void plane_unlock(struct wlr_eglstream_plane *plane) {
	if (!plane) {
		return;
	}
	if (plane->locks > 0 && --plane->locks > 0) {
		return;
	}
	wlr_log(WLR_INFO, "Destroying plane %u, %dx%d", plane->id,
		plane->width, plane->height);
	wlr_egl_destroy_eglstreams_surface(&plane->stream);
	wl_list_remove(&plane->link);
	plane->stream.stream = EGL_NO_STREAM_KHR;
	free(plane);
}

static void buffer_destroy(struct wlr_buffer *wlr_buffer) {
	struct wlr_eglstream_buffer *buffer =
		get_eglstresms_buffer_from_buffer(wlr_buffer);
	wlr_log(WLR_DEBUG, "Destroying %dx%d EGLStreams buffer",
		buffer->base.width, buffer->base.height);
	plane_unlock(buffer->plane);
	free(buffer);
}

static bool buffer_get_dmabuf(struct wlr_buffer *wlr_buffer,
		struct wlr_dmabuf_attributes *attribs) {
	// Disable dma-buf functiobality for EGLStreams.
	// TODO: Enable when nvidia driver is ready.
	wlr_log(WLR_ERROR, "Dma-Buf for EGLStreams is not supported");
	return false;
}

static const struct wlr_buffer_impl buffer_impl = {
	.destroy = buffer_destroy,
	.get_dmabuf = buffer_get_dmabuf,
};

static const struct wlr_allocator_interface allocator_impl;

static struct wlr_eglstreams_allocator *get_egstreams_alloc_from_alloc(
		struct wlr_allocator *alloc) {
	assert(alloc->impl == &allocator_impl);
	return (struct wlr_eglstreams_allocator *)alloc;
}


static void multi_backend_callback(struct wlr_backend *backend, void *data)
{
	struct wlr_eglstreams_allocator *alloc = (struct wlr_eglstreams_allocator *)data;
	if (wlr_backend_is_drm(backend))
	{
		alloc->drm = (struct wlr_drm_backend *)backend;
	}

}

struct wlr_allocator *wlr_eglstreams_allocator_create(struct wlr_backend *backend,
	struct wlr_renderer *renderer,
	uint32_t buffer_caps) {
	assert(wlr_backend_is_multi(backend));
	struct wlr_eglstreams_allocator *alloc = calloc(1, sizeof(*alloc));
	if (alloc == NULL) {
		return NULL;
	}
	wlr_allocator_init(&alloc->base, &allocator_impl, buffer_caps);

	alloc->drm = NULL;
	wlr_multi_for_each_backend(backend, multi_backend_callback, alloc);
	assert(alloc->drm);

	alloc->egl = wlr_gles2_renderer_get_egl(renderer);
	wl_list_init(&alloc->planes);
	wlr_log(WLR_DEBUG, "Created EGLStreams allocator");

	return &alloc->base;
}

static void allocator_destroy(struct wlr_allocator *wlr_alloc) {
	struct wlr_eglstreams_allocator *alloc =
		get_egstreams_alloc_from_alloc(wlr_alloc);

	free(alloc);
}

struct wlr_eglstream_plane *wlr_eglstream_plane_for_id(
		struct wlr_allocator *wlr_alloc, uint32_t plane_id) {
	struct wlr_eglstreams_allocator *alloc = get_egstreams_alloc_from_alloc(wlr_alloc);
	struct wlr_eglstream_plane *plane;
	bool found = false;
	wl_list_for_each(plane, &alloc->planes, link) {
		if (plane->id == plane_id) {
			found = true;
			break;
		}
	}
	return found ? plane : NULL;
}

static struct wlr_eglstream_plane *find_or_create_plane(
		struct wlr_eglstreams_allocator *alloc,
		int width, int height, uint32_t plane_id) {

	struct wlr_eglstream_plane *plane =
		wlr_eglstream_plane_for_id(&alloc->base, plane_id);

	struct wlr_egl *egl = alloc->egl;
	if (!egl) {
		return NULL;
	}

	if (plane) {
		if (width != plane->width || height != plane->height) {
			wlr_log(WLR_ERROR, "Found EGLStream plane size differs. "
				"%dx%d -> %dx%d (new)"
				"New plane will be created",
				plane->width, plane->height, width, height);
			plane = NULL;
		} else {
			wlr_log(WLR_INFO, "Found allocated plane %u, %dx%d", plane_id,
				plane->width, plane->height);
		}
	} else {
		plane = NULL;
	}

	if (plane == NULL) {
		plane = calloc(1, sizeof(*plane));
		if (!plane) {
			wlr_log(WLR_ERROR, "EGLStream plane allocation failed");
			return NULL;
		}
		plane->id = plane_id;
		plane->stream.drm = alloc->drm;
		plane->stream.egl = egl;
		plane->width = width;
		plane->height = height;
		if (!wlr_egl_create_eglstreams_surface(&plane->stream,
				plane_id, width, height)) {
			wlr_log(WLR_ERROR, "EGLStream setup failed for plane %u", plane_id);
			goto error;
		}
		wl_list_insert(&alloc->planes, &plane->link);
	}

	return plane;
error:
	free(plane);
	return NULL;

}

static struct wlr_buffer *allocator_create_buffer(
		struct wlr_allocator *wlr_alloc, int width, int height,
		const struct wlr_drm_format *format, void *data) {
	struct wlr_eglstreams_allocator *alloc =
		get_egstreams_alloc_from_alloc(wlr_alloc);
	// Note: every EGLStream buffer is just a pointer
	// to the only one real EGLStream for drm plane.
	struct wlr_eglstream_plane *plane =
		find_or_create_plane(alloc, width, height, (uint32_t)(long)data);
	if (!plane) {
		return NULL;
	}
	struct wlr_eglstream_buffer *buffer = create_buffer(alloc, plane);
	if (buffer == NULL) {
		return NULL;
	}
	return &buffer->base;
}
void wlr_eglstream_dispose_planes(struct wlr_allocator *wlr_alloc) {
	wlr_log(WLR_INFO, "Disposing eglstream planes...");
	struct wlr_eglstreams_allocator *alloc =
		get_egstreams_alloc_from_alloc(wlr_alloc);
	struct wlr_eglstream_plane *plane;
	wl_list_for_each(plane, &alloc->planes, link) {
		struct wlr_eglstream *egl_stream = &plane->stream;
		if(egl_stream->stream != EGL_NO_STREAM_KHR &&
			egl_stream->surface != EGL_NO_SURFACE) {
			wlr_egl_destroy_eglstreams_surface(egl_stream);
			wlr_log(WLR_INFO, "EGLStream plane %d has been disposed", plane->id);
		}

	}
}

void wlr_eglstream_recreate_planes(struct wlr_allocator *wlr_alloc) {
	wlr_log(WLR_INFO, "Recreating eglstream planes...");
	struct wlr_eglstreams_allocator *alloc =
		get_egstreams_alloc_from_alloc(wlr_alloc);
	struct wlr_eglstream_plane *plane;
	wl_list_for_each(plane, &alloc->planes, link) {
		struct wlr_eglstream *egl_stream = &plane->stream;
		if(egl_stream->stream == EGL_NO_STREAM_KHR &&
			egl_stream->surface == EGL_NO_SURFACE) {
			wlr_egl_create_eglstreams_surface(egl_stream, plane->id, plane->width, plane->height);
		}

	}
}

static const struct wlr_allocator_interface allocator_impl = {
	.destroy = allocator_destroy,
	.create_buffer = allocator_create_buffer,
};
