/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_RENDER_EGL_H
#define WLR_RENDER_EGL_H

#ifndef MESA_EGL_NO_X11_HEADERS
#define MESA_EGL_NO_X11_HEADERS
#endif
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

#ifndef EGL_NV_stream_attrib
#define EGL_NV_stream_attrib 1
typedef EGLStreamKHR (EGLAPIENTRYP PFNEGLCREATESTREAMATTRIBNVPROC)(EGLDisplay dpy, const EGLAttrib *attrib_list);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSETSTREAMATTRIBNVPROC)(EGLDisplay dpy, EGLStreamKHR stream, EGLenum attribute, EGLAttrib value);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLQUERYSTREAMATTRIBNVPROC)(EGLDisplay dpy, EGLStreamKHR stream, EGLenum attribute, EGLAttrib *value);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSTREAMCONSUMERACQUIREATTRIBNVPROC)(EGLDisplay dpy, EGLStreamKHR stream, const EGLAttrib *attrib_list);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSTREAMCONSUMERRELEASEATTRIBNVPROC)(EGLDisplay dpy, EGLStreamKHR stream, const EGLAttrib *attrib_list);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLQUERYSTREAMATTRIBNV)(EGLDisplay, EGLStreamKHR, EGLenum, EGLAttrib *);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSTREAMCONSUMERACQUIREKHR)(EGLDisplay, EGLStreamKHR);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSTREAMCONSUMERRELEASEKHR)(EGLDisplay, EGLStreamKHR);
#endif /* EGL_NV_stream_attrib */

#ifndef EGL_EXT_stream_acquire_mode
#define EGL_EXT_stream_acquire_mode 1
#define EGL_CONSUMER_AUTO_ACQUIRE_EXT 0x332B
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSTREAMCONSUMERACQUIREATTRIBEXTPROC)(EGLDisplay dpy, EGLStreamKHR stream, const EGLAttrib *attrib_list);
#endif /* EGL_EXT_stream_acquire_mode */

#ifndef EGL_NV_output_drm_flip_event
#define EGL_NV_output_drm_flip_event 1
#define EGL_DRM_FLIP_EVENT_DATA_NV 0x333E
#endif /* EGL_NV_output_drm_flip_event */

#ifndef EGL_DRM_MASTER_FD_EXT
#define EGL_DRM_MASTER_FD_EXT 0x333C
#endif /* EGL_DRM_MASTER_FD_EXT */

#ifndef EGL_WL_wayland_eglstream
#define EGL_WL_wayland_eglstream 1
#define EGL_WAYLAND_EGLSTREAM_WL 0x334B
#endif /* EGL_WL_wayland_eglstream */

#ifndef EGL_RESOURCE_BUSY_EXT
#define EGL_RESOURCE_BUSY_EXT 0x3353
#endif /* EGL_RESOURCE_BUSY_EXT */

struct wlr_eglstream {
	struct wlr_drm_backend *drm;
	struct wlr_egl *egl;
	EGLStreamKHR stream;
	EGLSurface surface;
	bool busy;
};

struct wlr_egl {
	EGLDisplay display;
	EGLContext context;
	EGLDeviceEXT device; // may be EGL_NO_DEVICE_EXT
	struct gbm_device *gbm_device;
	EGLConfig egl_config; // For setting up EGLStreams context
	struct wlr_eglstream *current_eglstream; // Non-null for EGLStream frame
	struct wl_display *wl_display;

	struct {
		// Display extensions
		bool KHR_image_base;
		bool EXT_image_dma_buf_import;
		bool EXT_image_dma_buf_import_modifiers;
		bool IMG_context_priority;

		// Device extensions
		bool EXT_device_drm;
		bool EXT_device_drm_render_node;

		// Client extensions
		bool EXT_device_query;
		bool KHR_platform_gbm;
		bool EXT_platform_device;
	} exts;

	struct {
		PFNEGLBINDWAYLANDDISPLAYWL eglBindWaylandDisplayWL;
		PFNEGLUNBINDWAYLANDDISPLAYWL eglUnbindWaylandDisplayWL;
		PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT;
		PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
		PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
		PFNEGLQUERYWAYLANDBUFFERWL eglQueryWaylandBufferWL;
		PFNEGLQUERYDMABUFFORMATSEXTPROC eglQueryDmaBufFormatsEXT;
		PFNEGLQUERYDMABUFMODIFIERSEXTPROC eglQueryDmaBufModifiersEXT;
		PFNEGLDEBUGMESSAGECONTROLKHRPROC eglDebugMessageControlKHR;
		PFNEGLQUERYDISPLAYATTRIBEXTPROC eglQueryDisplayAttribEXT;
		PFNEGLQUERYDEVICESTRINGEXTPROC eglQueryDeviceStringEXT;
		PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT;
		// EGLStreams
		PFNEGLGETOUTPUTLAYERSEXTPROC eglGetOutputLayersEXT;
		PFNEGLCREATESTREAMKHRPROC eglCreateStreamKHR;
		PFNEGLDESTROYSTREAMKHRPROC eglDestroyStreamKHR;
		PFNEGLSTREAMCONSUMEROUTPUTEXTPROC eglStreamConsumerOutputEXT;
		PFNEGLCREATESTREAMPRODUCERSURFACEKHRPROC eglCreateStreamProducerSurfaceKHR;
		PFNEGLSTREAMCONSUMERACQUIREATTRIBNVPROC eglStreamConsumerAcquireAttribNV;
		PFNEGLQUERYSTREAMATTRIBNV eglQueryStreamAttribNV;
		PFNEGLSETSTREAMATTRIBNVPROC eglSetStreamAttribNV;
		PFNEGLSTREAMCONSUMERACQUIREKHR eglStreamConsumerAcquireKHR;
		PFNEGLSTREAMCONSUMERRELEASEKHR eglStreamConsumerReleaseKHR;
		PFNEGLQUERYSTREAMKHRPROC eglQueryStreamKHR;
		PFNEGLCREATESTREAMATTRIBNVPROC eglCreateStreamAttribNV;
		PFNEGLSTREAMCONSUMERGLTEXTUREEXTERNALKHRPROC eglStreamConsumerGLTextureExternalKHR;
		PFNEGLSTREAMFLUSHNVPROC eglStreamFlushNV;
	} procs;

	bool has_modifiers;
	struct wlr_drm_format_set dmabuf_texture_formats;
	struct wlr_drm_format_set dmabuf_render_formats;

	bool is_eglstreams;
	int drm_fd;
};

struct wlr_egl *wlr_egl_create_with_context(EGLDisplay display,
	EGLContext context);

/**
 * Make the EGL context current.
 *
 * Callers are expected to clear the current context when they are done by
 * calling wlr_egl_unset_current.
 */
bool wlr_egl_make_current(struct wlr_egl *egl);

bool wlr_egl_unset_current(struct wlr_egl *egl);

bool wlr_egl_is_current(struct wlr_egl *egl);

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
