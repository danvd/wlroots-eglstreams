/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_RENDER_EGL_H
#define WLR_RENDER_EGL_H

#ifndef EGL_NO_X11
#define EGL_NO_X11
#endif
#ifndef EGL_NO_PLATFORM_SPECIFIC_TYPES
#define EGL_NO_PLATFORM_SPECIFIC_TYPES
#endif

#include <wlr/config.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <pixman.h>
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/render/dmabuf.h>
#include <wlr/render/drm_format_set.h>

struct wlr_egl;
struct wlr_eglstream;

/**
 * Create a struct wlr_egl with an existing EGL display and context.
 *
 * This is typically used by compositors which want to customize EGL
 * initialization.
 */
struct wlr_egl *wlr_egl_create_with_context(EGLDisplay display,
	EGLContext context);

/**
 * Get the EGL display used by the struct wlr_egl.
 *
 * This is typically used by compositors which need to perform custom OpenGL
 * operations.
 */
EGLDisplay wlr_egl_get_display(struct wlr_egl *egl);

/**
 * Get the EGL context used by the struct wlr_egl.
 *
 * This is typically used by compositors which need to perform custom OpenGL
 * operations.
 */
EGLContext wlr_egl_get_context(struct wlr_egl *egl);

/**
 * Sets up EGLSurface for passed egl_stream
 * Expects egl_stream->drm and egl_stream->egl to be valid
 */
bool wlr_egl_create_eglstreams_surface(struct wlr_eglstream *egl_stream,
		uint32_t plane_id, int width, int height);

/**
 * Destroys egl_stream and frees resources used
 */
void wlr_egl_destroy_eglstreams_surface(struct wlr_eglstream *egl_stream);

/**
 * Flips EGLStream for presentation. Updated buffers ages.
 * Expects EGLStream surface to be current
 */
struct wlr_output;
bool wlr_egl_flip_eglstreams_page(struct wlr_output *output);

/**
 * Load and initialized nvidia eglstream controller.
 * for mapping client EGL surfaces.
 */
void init_eglstream_controller(struct wl_display *display);

/**
 * Gets the parameters of wayland egl buffer.
 */
bool wlr_egl_wl_buffer_get_params(struct wlr_egl *egl,
	struct wl_resource *buffer, int *width, int *height, int *inverted_y);
#endif
