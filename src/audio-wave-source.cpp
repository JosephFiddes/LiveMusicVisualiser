#include "audio-wave-source.hpp"

#include <util/platform.h>

static struct obs_source_info auviz_source_info;

static void audio_capture_cb(void *param, obs_source_t *, const struct audio_data *audio, bool muted);
static void attach_to_audio_source(auviz_source *s);
static void detach_from_audio_source(auviz_source *s);
static void release_audio_weak(auviz_source *s);
static const char *auviz_source_get_name(void *data);
static void *auviz_source_create(obs_data_t *settings, obs_source_t *source);
static void auviz_source_destroy(void *data);
static void auviz_source_get_defaults(obs_data_t *settings);
static obs_properties_t *auviz_source_get_properties(void *data);
static void auviz_source_show(void *data);
static void auviz_source_hide(void *data);
static void auviz_source_update(void *data, obs_data_t *settings);
static void auviz_source_render(void *data, gs_effect_t *effect);
static uint32_t auviz_source_get_width(void *data);
static uint32_t auviz_source_get_height(void *data);

static void audio_capture_cb(void* param, obs_source_t*, const struct audio_data* audio, bool muted) {
	auto *s = static_cast<auviz_source *>(param);
	if (!s)
		return;

	if (!s->alive.load(std::memory_order_acquire))
		return;

	s->audio_cb_inflight.fetch_add(1, std::memory_order_acq_rel);

	if (s->alive.load(std::memory_order_acquire) && !muted && audio && audio->frames > 0) {
		// audio has been captured. processing...
		std::lock_guard<std::mutex> lock(s->audio_mutex);
		
		const size_t frames = audio->frames;

		const float *left = reinterpret_cast<const float *>(audio->data[0]);
		const float *right = audio->data[1] ? reinterpret_cast<const float *>(audio->data[1]) : nullptr;

		if (s->samples_left.size() < frames) {
			s->samples_left.resize(frames);
		}
		if (s->samples_right.size() < frames) {
			s->samples_right.resize(frames);
		}

		std::memcpy(s->samples_left.data(), left, frames * sizeof(float));
		if (right)
			std::memcpy(s->samples_right.data(), right, frames * sizeof(float));
		else
			std::memcpy(s->samples_right.data(), left, frames * sizeof(float));

		s->num_samples = audio->frames;
	}

	s->audio_cb_inflight.fetch_sub(1, std::memory_order_acq_rel);
}

static void attach_to_audio_source(auviz_source *s)
{
	if (!s)
		return;

	release_audio_weak(s);

	if (s->name.empty())
		return;

	obs_source_t *target = obs_get_source_by_name(s->name.c_str());
	if (!target) {
		obs_log(LOG_WARNING, "Audio source '%s' not found", s->name.c_str());
		return;
	}

	s->audio_weak = obs_source_get_weak_source(target);
	obs_source_add_audio_capture_callback(target, audio_capture_cb, s);

	obs_source_release(target);

	obs_log(LOG_INFO, "Attached to audio source '%s'", s->name.c_str());
}

static void detach_from_audio_source(auviz_source *s)
{
	if (!s || !s->audio_weak)
		return;

	obs_source_t *target = obs_weak_source_get_source(s->audio_weak);
	if (target) {
		obs_source_remove_audio_capture_callback(target, audio_capture_cb, s);
		obs_source_release(target);
	}

	release_audio_weak(s);
}

static void release_audio_weak(auviz_source *s)
{
	if (!s || !s->audio_weak)
		return;

	obs_weak_source_release(s->audio_weak);
	s->audio_weak = nullptr;
}

/* Get the name of the audio wave source */
static const char *auviz_source_get_name(void* data) {
	return obs_module_text("JDF's Audio Visualiser");
}

/* Create the audio wave source */
static void *auviz_source_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new auviz_source();

	s->self = source;

	auviz_source_update(s, settings);

	obs_log(LOG_INFO, "Source created");

	return s;
}

/* Destroy the audio wave source */
static void auviz_source_destroy(void *data)
{
	auto *s = static_cast<auviz_source *>(data);

	if (!s)
		return;

	s -> alive.store(false, std::memory_order_release);
	
	detach_from_audio_source(s);

	for (int i = 0; i < 2000; ++i) {
		if (s->audio_cb_inflight.load(std::memory_order_acquire) == 0)
			break;
		os_sleep_ms(1);
	}

	//if (s->theme && s->theme->destroy_data) {
	//	std::lock_guard<std::mutex> g(s->render_mutex);
	//	s->theme->destroy_data(s);
	//	s->theme_data = nullptr;
	//}

	{
		std::lock_guard<std::mutex> lock(s->audio_mutex);
		s->samples_left.clear();
		s->samples_right.clear();
		s->wave.clear();
		s->num_samples = 0;
	}

	delete s;
}

/* Set the default settings for the audio wave source */
static void auviz_source_get_defaults(obs_data_t *settings)
{
	// Get various default settings
}

/* Get the properties for the audio wave source */
static obs_properties_t *auviz_source_get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	return props;
}

/* Run when audio wave is shown */
static void auviz_source_show(void *data)
{
	auto *s = static_cast<auviz_source *>(data);
	if (!s)
		return;

	detach_from_audio_source(s);
	attach_to_audio_source(s);
}

/* Run when audio wave is hidden */
static void auviz_source_hide(void *data)
{
	auto *s = static_cast<auviz_source *>(data);
	if (!s)
		return;

	detach_from_audio_source(s);
}

/* Update the audio wave source settings */ 
static void auviz_source_update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<auviz_source *>(data);
	if (!s) 
		return;

	detach_from_audio_source(s);

	s->name = obs_data_get_string(settings, "audio_source");

	{
		std::lock_guard<std::mutex> g(s->render_mutex);

		s->width = static_cast<int>(obs_data_get_int(settings, "width"));
		// do more updaty stuff
	}

	attach_to_audio_source(s);
}

/* Render the audio wave source */
static void auviz_source_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	auto *s = static_cast<auviz_source *>(data);
	if (!s)
		return;

	{
		std::lock_guard<std::mutex> g(s->render_mutex);

		// Rendering implementation goes here
	}
}

/* Get the width of the audio wave source */
static uint32_t auviz_source_get_width(void *data)
{
	auto *s = static_cast<auviz_source *>(data);
	return s ? (uint32_t)s->width : 0;
}

/* Get the height of the audio wave source */
static uint32_t auviz_source_get_height(void *data)
{
	auto *s = static_cast<auviz_source *>(data);
	return s ? (uint32_t)s->height : 0;
}

void register_audio_viz_source() {
	std::memset(&auviz_source_info, 0, sizeof(auviz_source_info));

	auviz_source_info.id = "audio-viz";
	auviz_source_info.type = OBS_SOURCE_TYPE_INPUT;
	auviz_source_info.output_flags = OBS_SOURCE_VIDEO;

	auviz_source_info.get_name = auviz_source_get_name;
	auviz_source_info.icon_type = OBS_ICON_TYPE_PROCESS_AUDIO_OUTPUT;

	auviz_source_info.create = auviz_source_create;
	auviz_source_info.destroy = auviz_source_destroy;
	auviz_source_info.update = auviz_source_update;

	auviz_source_info.get_defaults = auviz_source_get_defaults;
	auviz_source_info.get_properties = auviz_source_get_properties;

	auviz_source_info.show = auviz_source_show;
	auviz_source_info.hide = auviz_source_hide;

	auviz_source_info.video_render = auviz_source_render;

	auviz_source_info.get_width = auviz_source_get_width;
	auviz_source_info.get_height = auviz_source_get_height;

	obs_register_source(&auviz_source_info);

	obs_log(LOG_INFO, "audio visualiser (%s: %s) registered.", auviz_source_info.id,
		auviz_source_info.get_name(0));
}