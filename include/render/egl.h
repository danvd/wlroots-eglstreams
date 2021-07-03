#ifndef RENDER_EGL_H
#define RENDER_EGL_H

#include <wlr/render/egl.h>

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
	struct wlr_eglstream *current_eglstream; // Non-null for EGLStream frame
	struct wl_display *wl_display;

	EGLConfig egl_config; // For setting up EGLStreams context

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
		PFNEGLBINDWAYLANDDISPLAYWL eglBindWaylandDisplayWL;
		PFNEGLUNBINDWAYLANDDISPLAYWL eglUnbindWaylandDisplayWL;
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

struct wlr_egl_context {
	EGLDisplay display;
	EGLContext context;
	EGLSurface draw_surface;
	EGLSurface read_surface;
};

/**
 * Initializes an EGL context for the given DRM FD.
 *
 * Will attempt to load all possibly required API functions.
 */
struct wlr_egl *wlr_egl_create_with_drm_fd(int drm_fd);

/**
 * Frees all related EGL resources, makes the context not-current and
 * unbinds a bound wayland display.
 */
void wlr_egl_destroy(struct wlr_egl *egl);

/**
 * Creates an EGL image from the given dmabuf attributes. Check usability
 * of the dmabuf with wlr_egl_check_import_dmabuf once first.
 */
EGLImageKHR wlr_egl_create_image_from_dmabuf(struct wlr_egl *egl,
	struct wlr_dmabuf_attributes *attributes, bool *external_only);

/**
 * Get DMA-BUF formats suitable for sampling usage.
 */
const struct wlr_drm_format_set *wlr_egl_get_dmabuf_texture_formats(
	struct wlr_egl *egl);
/**
 * Get DMA-BUF formats suitable for rendering usage.
 */
const struct wlr_drm_format_set *wlr_egl_get_dmabuf_render_formats(
	struct wlr_egl *egl);

/**
 * Destroys an EGL image created with the given wlr_egl.
 */
bool wlr_egl_destroy_image(struct wlr_egl *egl, EGLImageKHR image);

int wlr_egl_dup_drm_fd(struct wlr_egl *egl);

/**
 * Save the current EGL context to the structure provided in the argument.
 *
 * This includes display, context, draw surface and read surface.
 */
void wlr_egl_save_context(struct wlr_egl_context *context);

/**
 * Restore EGL context that was previously saved using wlr_egl_save_current().
 */
bool wlr_egl_restore_context(struct wlr_egl_context *context);

/**
 * Make the EGL context current.
 *
 * Callers are expected to clear the current context when they are done by
 * calling wlr_egl_unset_current().
 */
bool wlr_egl_make_current(struct wlr_egl *egl);

bool wlr_egl_unset_current(struct wlr_egl *egl);

bool wlr_egl_is_current(struct wlr_egl *egl);

bool wlr_egl_try_to_acquire_stream(struct wlr_egl *egl,
	EGLStreamKHR stream, const EGLAttrib *attrib_list);
#endif
