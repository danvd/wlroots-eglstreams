#include <assert.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/log.h>
#include "types/wlr_buffer.h"

static const struct wlr_buffer_impl client_buffer_impl;

struct wlr_client_buffer *wlr_client_buffer_get(struct wlr_buffer *buffer) {
	if (buffer->impl != &client_buffer_impl) {
		return NULL;
	}
	return (struct wlr_client_buffer *)buffer;
}

static struct wlr_client_buffer *client_buffer_from_buffer(
		struct wlr_buffer *buffer) {
	struct wlr_client_buffer *client_buffer = wlr_client_buffer_get(buffer);
	assert(client_buffer != NULL);
	return client_buffer;
}

static void client_buffer_destroy(struct wlr_buffer *buffer) {
	struct wlr_client_buffer *client_buffer = client_buffer_from_buffer(buffer);
	wl_list_remove(&client_buffer->source_destroy.link);
	wlr_texture_destroy(client_buffer->texture);
	free(client_buffer);
}

static bool client_buffer_get_dmabuf(struct wlr_buffer *buffer,
		struct wlr_dmabuf_attributes *attribs) {
	struct wlr_client_buffer *client_buffer = client_buffer_from_buffer(buffer);

	if (client_buffer->source == NULL) {
		return false;
	}

	return wlr_buffer_get_dmabuf(client_buffer->source, attribs);
}

static const struct wlr_buffer_impl client_buffer_impl = {
	.destroy = client_buffer_destroy,
	.get_dmabuf = client_buffer_get_dmabuf,
};

static void client_buffer_handle_source_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_client_buffer *client_buffer =
		wl_container_of(listener, client_buffer, source_destroy);
	wl_list_remove(&client_buffer->source_destroy.link);
	wl_list_init(&client_buffer->source_destroy.link);
	client_buffer->source = NULL;
}

struct wlr_client_buffer *wlr_client_buffer_create(struct wlr_buffer *buffer,
		struct wlr_renderer *renderer) {
	struct wlr_texture *texture = wlr_texture_from_buffer(renderer, buffer);
	if (texture == NULL) {
		wlr_log(WLR_ERROR, "Failed to create texture");
		return NULL;
	}

	struct wlr_client_buffer *client_buffer =
		calloc(1, sizeof(struct wlr_client_buffer));
	if (client_buffer == NULL) {
		wlr_texture_destroy(texture);
		return NULL;
	}
	wlr_buffer_init(&client_buffer->base, &client_buffer_impl,
		texture->width, texture->height);
	client_buffer->source = buffer;
	client_buffer->texture = texture;
	client_buffer->base.egl_stream = buffer->egl_stream;

	wl_signal_add(&buffer->events.destroy, &client_buffer->source_destroy);
	client_buffer->source_destroy.notify = client_buffer_handle_source_destroy;

	// Ensure the buffer will be released before being destroyed
	wlr_buffer_lock(&client_buffer->base);
	wlr_buffer_drop(&client_buffer->base);

	return client_buffer;
}

bool wlr_client_buffer_apply_damage(struct wlr_client_buffer *client_buffer,
		struct wlr_buffer *next, pixman_region32_t *damage) {
	if (client_buffer->base.n_locks > 1) {
		// Someone else still has a reference to the buffer
		return false;
	}

	return wlr_texture_update_from_buffer(client_buffer->texture, next, damage);
}
