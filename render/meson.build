renderers = get_option('renderers')
if 'auto' in renderers and get_option('auto_features').enabled()
	renderers = ['gles2', 'vulkan']
elif 'auto' in renderers and get_option('auto_features').disabled()
	renderers = []
endif

if 'gles2' not in renderers
	renderers += 'gles2'
endif

wlr_files += files(
	'dmabuf.c',
	'drm_format_set.c',
	'pixel_format.c',
	'swapchain.c',
	'wlr_renderer.c',
	'wlr_texture.c',
	'eglstreams_allocator.c',
)

if 'gles2' in renderers or 'auto' in renderers
	egl = dependency('egl', required: 'gles2' in renderers)
	gbm = dependency('gbm', required: 'gles2' in renderers)
	if egl.found() and gbm.found()
		wlr_deps += [egl, gbm]
		wlr_files += files('egl.c')
		internal_features += { 'egl': true }
	endif
	subdir('gles2')
endif

if 'vulkan' in renderers or 'auto' in renderers
	subdir('vulkan')
endif

subdir('pixman')

subdir('allocator')
