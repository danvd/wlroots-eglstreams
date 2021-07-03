#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/interface.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include "backend/backend.h"
#include "render/allocator/allocator.h"
#include "render/drm_format_set.h"
#include "render/swapchain.h"
#include "render/wlr_renderer.h"
#include "render/pixel_format.h"
#include "types/wlr_output.h"
#include "backend/drm/drm.h"

bool wlr_output_init_render(struct wlr_output *output,
		struct wlr_allocator *allocator, struct wlr_renderer *renderer) {
	assert(output->allocator == NULL && allocator != NULL);
	assert(output->renderer == NULL && renderer != NULL);

	uint32_t backend_caps = backend_get_buffer_caps(output->backend);
	uint32_t renderer_caps = renderer_get_render_buffer_caps(renderer);

	if (!(backend_caps & allocator->buffer_caps)) {
		wlr_log(WLR_ERROR, "output backend and allocator buffer capabilities "
			"don't match");
		return false;
	} else if (!(renderer_caps & allocator->buffer_caps)) {
		wlr_log(WLR_ERROR, "renderer and allocator buffer capabilities "
			"don't match");
		return false;
	}

	output->allocator = allocator;
	output->renderer = renderer;

	return true;
}

/**
 * Ensure the output has a suitable swapchain. The swapchain is re-created if
 * necessary.
 *
 * If allow_modifiers is set to true, the swapchain's format may use modifiers.
 * If set to false, the swapchain's format is guaranteed to not use modifiers.
 */
static bool output_create_swapchain(struct wlr_output *output,
		const struct wlr_output_state *state, bool allow_modifiers) {
	int width, height;
	output_pending_resolution(output, state, &width, &height);

	struct wlr_allocator *allocator = output->allocator;
	assert(allocator != NULL);

	const struct wlr_drm_format_set *display_formats =
		wlr_output_get_primary_formats(output, allocator->buffer_caps);
	struct wlr_drm_format *format = output_pick_format(output, display_formats,
		output->render_format);
	if (format == NULL) {
		wlr_log(WLR_ERROR, "Failed to pick primary buffer format for output '%s'",
			output->name);
		return false;
	}

	if (output->swapchain != NULL && output->swapchain->width == width &&
			output->swapchain->height == height &&
			output->swapchain->format->format == format->format &&
			(allow_modifiers || output->swapchain->format->len == 0)) {
		// no change, keep existing swapchain
		free(format);
		return true;
	}

	char *format_name = drmGetFormatName(format->format);
	wlr_log(WLR_DEBUG, "Choosing primary buffer format %s (0x%08"PRIX32") for output '%s'",
		format_name ? format_name : "<unknown>", format->format, output->name);
	free(format_name);

	if (!allow_modifiers && (format->len != 1 || format->modifiers[0] != DRM_FORMAT_MOD_LINEAR)) {
		if (!wlr_drm_format_has(format, DRM_FORMAT_MOD_INVALID)) {
			wlr_log(WLR_DEBUG, "Implicit modifiers not supported");
			free(format);
			return false;
		}

		format->len = 0;
		wlr_drm_format_add(&format, DRM_FORMAT_MOD_INVALID);
	}

	void *plane_id = NULL;

	if (wlr_output_is_drm(output)) {
		plane_id = (void *)(long)((struct wlr_drm_connector *)output)->crtc->primary->id;
	}

	struct wlr_swapchain *swapchain =
		wlr_swapchain_create(allocator, width, height, format, plane_id);
	free(format);
	if (swapchain == NULL) {
		wlr_log(WLR_ERROR, "Failed to create output swapchain");
		return false;
	}

	wlr_swapchain_destroy(output->swapchain);
	output->swapchain = swapchain;

	return true;
}

static bool output_attach_back_buffer(struct wlr_output *output,
		const struct wlr_output_state *state, int *buffer_age) {
	assert(output->back_buffer == NULL);

	if (!output_create_swapchain(output, state, true)) {
		return false;
	}

	struct wlr_renderer *renderer = output->renderer;
	assert(renderer != NULL);

	struct wlr_buffer *buffer =
		wlr_swapchain_acquire(output->swapchain, buffer_age);
	if (buffer == NULL) {
		return false;
	}

	if (!renderer_bind_buffer(renderer, buffer)) {
		wlr_buffer_unlock(buffer);
		return false;
	}

	output->back_buffer = buffer;
	return true;
}

void output_clear_back_buffer(struct wlr_output *output) {
	if (output->back_buffer == NULL) {
		return;
	}

	struct wlr_renderer *renderer = output->renderer;
	assert(renderer != NULL);

	renderer_bind_buffer(renderer, NULL);

	wlr_buffer_unlock(output->back_buffer);
	output->back_buffer = NULL;
}

bool wlr_output_attach_render(struct wlr_output *output, int *buffer_age) {
	return output_attach_back_buffer(output, &output->pending, buffer_age);
}

static bool output_attach_empty_back_buffer(struct wlr_output *output,
		const struct wlr_output_state *state) {
	assert(!(state->committed & WLR_OUTPUT_STATE_BUFFER));

	if (!output_attach_back_buffer(output, state, NULL)) {
		return false;
	}

	int width, height;
	output_pending_resolution(output, state, &width, &height);

	struct wlr_renderer *renderer = output->renderer;
	wlr_renderer_begin(renderer, width, height);
	wlr_renderer_clear(renderer, (float[]){0, 0, 0, 0});
	wlr_renderer_end(renderer);

	return true;
}

static bool output_test_with_back_buffer(struct wlr_output *output,
		const struct wlr_output_state *state) {
	if (output->impl->test == NULL) {
		return true;
	}

	// Create a shallow copy of the state with the empty back buffer included
	// to pass to the backend.
	struct wlr_output_state copy = *state;
	assert((copy.committed & WLR_OUTPUT_STATE_BUFFER) == 0);
	copy.committed |= WLR_OUTPUT_STATE_BUFFER;
	assert(output->back_buffer != NULL);
	copy.buffer = output->back_buffer;

	return output->impl->test(output, &copy);
}

// This function may attach a new, empty back buffer if necessary.
// If so, the new_back_buffer out parameter will be set to true.
bool output_ensure_buffer(struct wlr_output *output,
		const struct wlr_output_state *state,
		bool *new_back_buffer) {
	assert(*new_back_buffer == false);

	// If we already have a buffer, we don't need to allocate a new one
	if (state->committed & WLR_OUTPUT_STATE_BUFFER) {
		return true;
	}

	// If the compositor hasn't called wlr_output_init_render(), they will use
	// their own logic to attach buffers
	if (output->renderer == NULL) {
		return true;
	}

	// If we're lighting up an output or changing its mode, make sure to
	// provide a new buffer
	bool needs_new_buffer = false;
	if ((state->committed & WLR_OUTPUT_STATE_ENABLED) && state->enabled) {
		needs_new_buffer = true;
	}
	if (state->committed & WLR_OUTPUT_STATE_MODE) {
		needs_new_buffer = true;
	}
	if (state->committed & WLR_OUTPUT_STATE_RENDER_FORMAT) {
		needs_new_buffer = true;
	}
	if (state->allow_artifacts && output->commit_seq == 0) {
		// On first commit, require a new buffer if the compositor called a
		// mode-setting function, even if the mode won't change. This makes it
		// so the swapchain is created now.
		needs_new_buffer = true;
	}
	if (!needs_new_buffer) {
		return true;
	}

	wlr_log(WLR_DEBUG, "Attaching empty buffer to output for modeset");

	if (!output_attach_empty_back_buffer(output, state)) {
		return false;
	}

	if (output_test_with_back_buffer(output, state)) {
		*new_back_buffer = true;
		return true;
	}

	output_clear_back_buffer(output);

	if (output->swapchain->format->len == 0) {
		return false;
	}

	// The test failed for a buffer which has modifiers, try disabling
	// modifiers to see if that makes a difference.
	wlr_log(WLR_DEBUG, "Output modeset test failed, retrying without modifiers");

	if (!output_create_swapchain(output, state, false)) {
		return false;
	}

	if (!output_attach_empty_back_buffer(output, state)) {
		goto error_destroy_swapchain;
	}

	if (output_test_with_back_buffer(output, state)) {
		*new_back_buffer = true;
		return true;
	}

	output_clear_back_buffer(output);

error_destroy_swapchain:
	// Destroy the modifierless swapchain so that the output does not get stuck
	// without modifiers. A new swapchain with modifiers will be created when
	// needed by output_attach_back_buffer().
	wlr_swapchain_destroy(output->swapchain);
	output->swapchain = NULL;

	return false;
}

void wlr_output_lock_attach_render(struct wlr_output *output, bool lock) {
	if (lock) {
		++output->attach_render_locks;
	} else {
		assert(output->attach_render_locks > 0);
		--output->attach_render_locks;
	}
	wlr_log(WLR_DEBUG, "%s direct scan-out on output '%s' (locks: %d)",
		lock ? "Disabling" : "Enabling", output->name,
		output->attach_render_locks);
}

struct wlr_drm_format *output_pick_format(struct wlr_output *output,
		const struct wlr_drm_format_set *display_formats,
		uint32_t fmt) {
	struct wlr_renderer *renderer = output->renderer;
	struct wlr_allocator *allocator = output->allocator;
	assert(renderer != NULL && allocator != NULL);

	const struct wlr_drm_format_set *render_formats =
		wlr_renderer_get_render_formats(renderer);
	if (render_formats == NULL) {
		wlr_log(WLR_ERROR, "Failed to get render formats");
		return NULL;
	}

	const struct wlr_drm_format *render_format =
		wlr_drm_format_set_get(render_formats, fmt);
	if (render_format == NULL) {
		wlr_log(WLR_DEBUG, "Renderer doesn't support format 0x%"PRIX32, fmt);
		return NULL;
	}

	struct wlr_drm_format *format = NULL;
	if (display_formats != NULL) {
		const struct wlr_drm_format *display_format =
			wlr_drm_format_set_get(display_formats, fmt);
		if (display_format == NULL) {
			wlr_log(WLR_DEBUG, "Output doesn't support format 0x%"PRIX32, fmt);
			return NULL;
		}
		format = wlr_drm_format_intersect(display_format, render_format);
	} else {
		// The output can display any format
		format = wlr_drm_format_dup(render_format);
	}

	if (format == NULL) {
		wlr_log(WLR_DEBUG, "Failed to intersect display and render "
			"modifiers for format 0x%"PRIX32 " on output %s",
			fmt, output->name);
		return NULL;
	}

	return format;
}

uint32_t wlr_output_preferred_read_format(struct wlr_output *output) {
	struct wlr_renderer *renderer = output->renderer;
	assert(renderer != NULL);

	if (!renderer->impl->preferred_read_format || !renderer->impl->read_pixels) {
		return DRM_FORMAT_INVALID;
	}

	if (!output_attach_back_buffer(output, &output->pending, NULL)) {
		return false;
	}

	uint32_t fmt = renderer->impl->preferred_read_format(renderer);

	output_clear_back_buffer(output);

	return fmt;
}

bool output_is_direct_scanout(struct wlr_output *output,
		struct wlr_buffer *buffer) {
	if (output->swapchain == NULL) {
		return true;
	}

	for (size_t i = 0; i < WLR_SWAPCHAIN_CAP; i++) {
		if (output->swapchain->slots[i].buffer == buffer) {
			return false;
		}
	}

	return true;
}
