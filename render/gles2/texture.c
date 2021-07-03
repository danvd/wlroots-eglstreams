#include <assert.h>
#include <drm_fourcc.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/render/egl.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include "render/egl.h"
#include "render/gles2.h"
#include "render/pixel_format.h"
#include "types/wlr_buffer.h"
#include "util/signal.h"

static const struct wlr_texture_impl texture_impl;

bool wlr_texture_is_gles2(struct wlr_texture *wlr_texture) {
	return wlr_texture->impl == &texture_impl;
}

struct wlr_gles2_texture *gles2_get_texture(
		struct wlr_texture *wlr_texture) {
	assert(wlr_texture_is_gles2(wlr_texture));
	return (struct wlr_gles2_texture *)wlr_texture;
}

static bool gles2_texture_is_opaque(struct wlr_texture *wlr_texture) {
	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);
	return !texture->has_alpha;
}

static bool check_stride(const struct wlr_pixel_format_info *fmt,
		uint32_t stride, uint32_t width) {
	if (stride % (fmt->bpp / 8) != 0) {
		wlr_log(WLR_ERROR, "Invalid stride %d (incompatible with %d "
			"bytes-per-pixel)", stride, fmt->bpp / 8);
		return false;
	}
	if (stride < width * (fmt->bpp / 8)) {
		wlr_log(WLR_ERROR, "Invalid stride %d (too small for %d "
			"bytes-per-pixel and width %d)", stride, fmt->bpp / 8, width);
		return false;
	}
	return true;
}

static bool gles2_texture_write_pixels(struct wlr_texture *wlr_texture,
		uint32_t stride, uint32_t width, uint32_t height,
		uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y,
		const void *data) {
	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);

	if (texture->target != GL_TEXTURE_2D || texture->image != EGL_NO_IMAGE_KHR) {
		wlr_log(WLR_ERROR, "Cannot write pixels to immutable texture");
		return false;
	}

	const struct wlr_gles2_pixel_format *fmt =
		get_gles2_format_from_drm(texture->drm_format);
	assert(fmt);

	const struct wlr_pixel_format_info *drm_fmt =
		drm_get_pixel_format_info(texture->drm_format);
	assert(drm_fmt);

	if (!check_stride(drm_fmt, stride, width)) {
		return false;
	}

	struct wlr_egl_context prev_ctx;
	wlr_egl_save_context(&prev_ctx);
	wlr_egl_make_current(texture->renderer->egl);

	push_gles2_debug(texture->renderer);

	glBindTexture(GL_TEXTURE_2D, texture->tex);

	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride / (drm_fmt->bpp / 8));
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, src_x);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, src_y);

	glTexSubImage2D(GL_TEXTURE_2D, 0, dst_x, dst_y, width, height,
		fmt->gl_format, fmt->gl_type, data);

	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0);

	glBindTexture(GL_TEXTURE_2D, 0);

	pop_gles2_debug(texture->renderer);

	wlr_egl_restore_context(&prev_ctx);

	return true;
}

static bool gles2_texture_invalidate(struct wlr_gles2_texture *texture) {
	if (texture->image == EGL_NO_IMAGE_KHR) {
		return false;
	}
	if (texture->target == GL_TEXTURE_EXTERNAL_OES) {
		// External changes are immediately made visible by the GL implementation
		return true;
	}

	struct wlr_egl_context prev_ctx;
	wlr_egl_save_context(&prev_ctx);
	wlr_egl_make_current(texture->renderer->egl);

	push_gles2_debug(texture->renderer);

	glBindTexture(texture->target, texture->tex);
	texture->renderer->procs.glEGLImageTargetTexture2DOES(texture->target,
		texture->image);
	glBindTexture(texture->target, 0);

	pop_gles2_debug(texture->renderer);

	wlr_egl_restore_context(&prev_ctx);

	return true;
}

void gles2_texture_destroy(struct wlr_gles2_texture *texture) {
	wl_list_remove(&texture->link);
	wl_list_remove(&texture->buffer_destroy.link);

	struct wlr_egl_context prev_ctx;
	wlr_egl_save_context(&prev_ctx);
	wlr_egl_make_current(texture->renderer->egl);

	push_gles2_debug(texture->renderer);
	if (!texture->stream) {
		glDeleteTextures(1, &texture->tex);
	}

	if (texture->image) {
		wlr_egl_destroy_image(texture->renderer->egl, texture->image);
	}

	pop_gles2_debug(texture->renderer);

	wlr_egl_restore_context(&prev_ctx);

	free(texture);
}

static void gles2_texture_unref(struct wlr_texture *wlr_texture) {
	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);
	if (texture->buffer != NULL) {
		// Keep the texture around, in case the buffer is re-used later. We're
		// still listening to the buffer's destroy event.
		wlr_buffer_unlock(texture->buffer);
	} else {
		gles2_texture_destroy(texture);
	}
}

static const struct wlr_texture_impl texture_impl = {
	.is_opaque = gles2_texture_is_opaque,
	.write_pixels = gles2_texture_write_pixels,
	.destroy = gles2_texture_unref,
};

static struct wlr_gles2_texture *gles2_texture_create(
		struct wlr_gles2_renderer *renderer, uint32_t width, uint32_t height) {
	struct wlr_gles2_texture *texture =
		calloc(1, sizeof(struct wlr_gles2_texture));
	if (texture == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	wlr_texture_init(&texture->wlr_texture, &texture_impl, width, height);
	texture->renderer = renderer;
	wl_list_insert(&renderer->textures, &texture->link);
	wl_list_init(&texture->buffer_destroy.link);
	texture->stream = EGL_NO_STREAM_KHR;
	return texture;
}

static struct wlr_texture *gles2_texture_from_pixels(
		struct wlr_renderer *wlr_renderer,
		uint32_t drm_format, uint32_t stride, uint32_t width,
		uint32_t height, const void *data) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);

	const struct wlr_gles2_pixel_format *fmt =
		get_gles2_format_from_drm(drm_format);
	if (fmt == NULL) {
		wlr_log(WLR_ERROR, "Unsupported pixel format 0x%"PRIX32, drm_format);
		return NULL;
	}

	const struct wlr_pixel_format_info *drm_fmt =
		drm_get_pixel_format_info(drm_format);
	assert(drm_fmt);

	if (!check_stride(drm_fmt, stride, width)) {
		return NULL;
	}

	struct wlr_gles2_texture *texture =
		gles2_texture_create(renderer, width, height);
	if (texture == NULL) {
		return NULL;
	}
	texture->target = GL_TEXTURE_2D;
	texture->has_alpha = fmt->has_alpha;
	texture->drm_format = fmt->drm_format;

	struct wlr_egl_context prev_ctx;
	wlr_egl_save_context(&prev_ctx);
	wlr_egl_make_current(renderer->egl);

	push_gles2_debug(renderer);

	glGenTextures(1, &texture->tex);
	glBindTexture(GL_TEXTURE_2D, texture->tex);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride / (drm_fmt->bpp / 8));
	glTexImage2D(GL_TEXTURE_2D, 0, fmt->gl_format, width, height, 0,
		fmt->gl_format, fmt->gl_type, data);
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);

	glBindTexture(GL_TEXTURE_2D, 0);

	pop_gles2_debug(renderer);

	wlr_egl_restore_context(&prev_ctx);

	return &texture->wlr_texture;
}

static struct wlr_texture *gles2_texture_from_dmabuf(
		struct wlr_renderer *wlr_renderer,
		struct wlr_dmabuf_attributes *attribs) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);

	if (!renderer->procs.glEGLImageTargetTexture2DOES) {
		return NULL;
	}

	struct wlr_gles2_texture *texture =
		gles2_texture_create(renderer, attribs->width, attribs->height);
	if (texture == NULL) {
		return NULL;
	}
	texture->has_alpha = true;
	texture->drm_format = DRM_FORMAT_INVALID; // texture can't be written anyways
	texture->inverted_y =
		(attribs->flags & WLR_DMABUF_ATTRIBUTES_FLAGS_Y_INVERT) != 0;

	struct wlr_egl_context prev_ctx;
	wlr_egl_save_context(&prev_ctx);
	wlr_egl_make_current(renderer->egl);

	bool external_only;
	texture->image =
		wlr_egl_create_image_from_dmabuf(renderer->egl, attribs, &external_only);
	if (texture->image == EGL_NO_IMAGE_KHR) {
		wlr_log(WLR_ERROR, "Failed to create EGL image from DMA-BUF");
		wlr_egl_restore_context(&prev_ctx);
		wl_list_remove(&texture->link);
		free(texture);
		return NULL;
	}

	texture->target = external_only ? GL_TEXTURE_EXTERNAL_OES : GL_TEXTURE_2D;

	push_gles2_debug(renderer);

	glGenTextures(1, &texture->tex);
	glBindTexture(texture->target, texture->tex);
	glTexParameteri(texture->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(texture->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	renderer->procs.glEGLImageTargetTexture2DOES(texture->target, texture->image);
	glBindTexture(texture->target, 0);

	pop_gles2_debug(renderer);

	wlr_egl_restore_context(&prev_ctx);

	return &texture->wlr_texture;
}

static void texture_handle_buffer_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_gles2_texture *texture =
		wl_container_of(listener, texture, buffer_destroy);
	gles2_texture_destroy(texture);
}

static struct wlr_texture *gles2_texture_from_dmabuf_buffer(
		struct wlr_gles2_renderer *renderer, struct wlr_buffer *buffer,
		struct wlr_dmabuf_attributes *dmabuf) {
	struct wlr_gles2_texture *texture;
	wl_list_for_each(texture, &renderer->textures, link) {
		if (texture->buffer == buffer) {
			if (!gles2_texture_invalidate(texture)) {
				wlr_log(WLR_ERROR, "Failed to invalidate texture");
				return false;
			}
			wlr_buffer_lock(texture->buffer);
			return &texture->wlr_texture;
		}
	}

	struct wlr_texture *wlr_texture =
		gles2_texture_from_dmabuf(&renderer->wlr_renderer, dmabuf);
	if (wlr_texture == NULL) {
		return false;
	}

	texture = gles2_get_texture(wlr_texture);
	texture->buffer = wlr_buffer_lock(buffer);

	texture->buffer_destroy.notify = texture_handle_buffer_destroy;
	wl_signal_add(&buffer->events.destroy, &texture->buffer_destroy);

	return &texture->wlr_texture;
}

struct wlr_texture *gles2_texture_from_buffer(struct wlr_renderer *wlr_renderer,
		struct wlr_buffer *buffer) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);

	void *data;
	uint32_t format;
	size_t stride;
	struct wlr_dmabuf_attributes dmabuf;
	if (wlr_buffer_get_dmabuf(buffer, &dmabuf)) {
		return gles2_texture_from_dmabuf_buffer(renderer, buffer, &dmabuf);
	} else if (buffer_begin_data_ptr_access(buffer, &data, &format, &stride)) {
		struct wlr_texture *tex = gles2_texture_from_pixels(wlr_renderer,
			format, stride, buffer->width, buffer->height, data);
		buffer_end_data_ptr_access(buffer);
		return tex;
	} else {
		return NULL;
	}
}

static void gles2_client_egl_stream_destroy(struct wl_listener *listener, void *data) {
	struct wlr_egl_client_stream *client_stream =
		wl_container_of(listener, client_stream, destroy_listener);
	struct wlr_egl_context prev_ctx;
	struct wlr_egl *egl = client_stream->renderer->egl;
	wlr_egl_save_context(&prev_ctx);
	wlr_egl_make_current(client_stream->renderer->egl);
	egl->procs.eglDestroyStreamKHR(egl->display, client_stream->stream);
	glDeleteTextures(1, &client_stream->tex);
	wlr_egl_restore_context(&prev_ctx);
	wl_list_remove(&client_stream->destroy_listener.link);
	wl_list_remove(&client_stream->link);
	free(client_stream);
}

struct wlr_texture *gles2_texture_from_wl_eglstream(struct wlr_renderer *wlr_renderer,
		struct wl_resource *resource) {
	int width, height;
	EGLint inverted_y;
	if (!wlr_egl_wl_buffer_get_params(gles2_renderer_get_egl(wlr_renderer), resource,
				&width, &height, &inverted_y)) {
		return NULL;
	}

	struct wlr_egl_context prev_ctx;
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);
	struct wlr_egl *egl = renderer->egl;
	wlr_egl_save_context(&prev_ctx);
	wlr_egl_make_current(renderer->egl);
	EGLAttrib stream_attribs[] = {
		EGL_WAYLAND_EGLSTREAM_WL, (EGLAttrib)resource,
		EGL_NONE
	};

	EGLStreamKHR stream = EGL_NO_STREAM_KHR;

	struct wlr_egl_client_stream *attached_stream = NULL;
	struct wlr_egl_client_stream *tmp;
	wl_list_for_each(tmp, &renderer->client_streams, link) {
		if (tmp->resource == resource) {
			attached_stream = tmp;
			break;
		}
	}

	if (attached_stream) {
		stream = attached_stream->stream;
	} else {
		stream = egl->procs.eglCreateStreamAttribNV(
				egl->display, stream_attribs);
	}

	if (stream == EGL_NO_STREAM_KHR) {
		goto error_ctx;
	}

	struct wlr_gles2_texture *texture =
		gles2_texture_create(renderer, width, height);
	if (texture == NULL) {
		wlr_log(WLR_ERROR, "Texture allocation failed");
		goto error_stream;
	}
	texture->renderer = renderer;

	texture->drm_format = DRM_FORMAT_INVALID;
	texture->image = NULL;
	texture->stream = stream;
	texture->inverted_y = !!!inverted_y;
	texture->has_alpha = true;
	texture->target = GL_TEXTURE_EXTERNAL_OES;

	push_gles2_debug(renderer);

	bool ok = false;
	if (attached_stream) {
		texture->tex = attached_stream->tex;
		ok = true;
		if (attached_stream->acquired) {
			egl->procs.eglStreamConsumerReleaseKHR(
				egl->display, attached_stream->stream);
		}
	} else {
		glGenTextures(1, &texture->tex);
		glBindTexture(texture->target, texture->tex);
		glTexParameteri(texture->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(texture->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		if(egl->procs.eglStreamConsumerGLTextureExternalKHR(
				egl->display, stream) == EGL_TRUE) {
			attached_stream = calloc(1, sizeof(*attached_stream));
			if (attached_stream) {
				attached_stream->renderer = renderer;
				attached_stream->stream = stream;
				attached_stream->acquired = false;
				attached_stream->resource = resource;
				attached_stream->tex = texture->tex;
				attached_stream->destroy_listener.notify =
					gles2_client_egl_stream_destroy;
				wl_resource_add_destroy_listener(resource,
						&attached_stream->destroy_listener);
				wl_list_insert(&renderer->client_streams,
						&attached_stream->link);
				ok = true;
			}
		}
		glBindTexture(texture->target, 0);

		if (!ok) {
			goto error_texture;
		}
	}

	if (ok) {
		egl->procs.eglStreamFlushNV(egl->display, stream);
		attached_stream->acquired =
			egl->procs.eglStreamConsumerAcquireAttribNV(egl->display, stream, NULL) == EGL_TRUE;
	}


	pop_gles2_debug(renderer);
	wlr_egl_restore_context(&prev_ctx);

	if (!ok) {
		wlr_log(WLR_ERROR, "Could not bind EGLStream to GL texture");
		goto error_texture;
	}

	return &texture->wlr_texture;

error_texture:
	wl_list_remove(&texture->link);
	glDeleteTextures(1, &texture->tex);
	free(texture);
error_stream:
	egl->procs.eglDestroyStreamKHR(
		egl->display, stream);
error_ctx:
	wlr_egl_restore_context(&prev_ctx);
	return NULL;
}

void wlr_gles2_texture_get_attribs(struct wlr_texture *wlr_texture,
		struct wlr_gles2_texture_attribs *attribs) {
	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);
	memset(attribs, 0, sizeof(*attribs));
	attribs->target = texture->target;
	attribs->tex = texture->tex;
	attribs->inverted_y = texture->inverted_y;
	attribs->has_alpha = texture->has_alpha;
}
