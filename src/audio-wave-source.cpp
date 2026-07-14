#include "audio-wave-source.hpp"

#include <util/platform.h>

#define AUVIZ_ID "audio-viz"

#define SHADER_FILEPATH_NAME "shader_filepath"

#define PI 3.14159265f

////// Function Declarations

static struct obs_source_info auviz_source_info;
static std::vector<auviz_parameter_behaviour *> parameter_behaviours;

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
static bool compile_effect(auviz_source *s);
static gs_eparam_t *gs_effect_get_param_by_name_ensure_success(gs_effect_t *effect, const char *name);

static std::vector<float> fft(const std::vector<float> & samples);

////// Function Definitions

static void add_parameter(std::string name, std::string disp_name, 
	void (*on_create)(std::string name, std::string disp_name, auviz_source *s),
	void (*get_default)(std::string name, std::string disp_name, obs_data_t *settings), 
	void (*get_properties)(std::string name, std::string disp_name, obs_properties_t *props, 
							auviz_source *s), 
	void (*on_update)(std::string name, std::string disp_name, auviz_source *s,
					    obs_data_t *settings),
	void (*on_video_render)(std::string name, std::string disp_name, auviz_source *s)
);

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

	if (s->audio_source_name.empty())
		return;

	obs_source_t *target = obs_get_source_by_name(s->audio_source_name.c_str());
	if (!target) {
		obs_log(LOG_WARNING, "Audio source '%s' not found", s->audio_source_name.c_str());
		return;
	}

	s->audio_weak = obs_source_get_weak_source(target);
	obs_source_add_audio_capture_callback(target, audio_capture_cb, s);

	obs_source_release(target);

	obs_log(LOG_INFO, "Attached to audio source '%s'", s->audio_source_name.c_str());
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

	s->shader_filepath = obs_data_get_string(settings, SHADER_FILEPATH_NAME);

	// Implementation code goes here.
	// Compile the shader effect and get parameter references.
	compile_effect(s);

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

	obs_enter_graphics();
	gs_texture_destroy(s->audio_freq_left_texture);
	gs_texture_destroy(s->audio_freq_right_texture);
	obs_leave_graphics();

	delete s;
}

/* Set the default settings for the audio wave source */
static void auviz_source_get_defaults(obs_data_t *settings)
{
	for (int i = 0; i < parameter_behaviours.size(); ++i) {
		auviz_parameter_behaviour *param = parameter_behaviours[i];
		if (param->get_default)
			param->get_default(param->name, param->disp_name, settings);
	}
}

/* Get the properties for the audio wave source */
static obs_properties_t *auviz_source_get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	auto *s = static_cast<auviz_source *>(data);
	if (!s)
		return props;

	for (int i = 0; i < parameter_behaviours.size(); ++i) {
		auviz_parameter_behaviour *param = parameter_behaviours[i];
		if (param->get_properties)
			param->get_properties(param->name, param->disp_name, props, s);
	}

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

	{
		std::lock_guard<std::mutex> g(s->render_mutex);

		s->width = static_cast<int>(obs_data_get_int(settings, "width"));
		// do more updaty stuff

		for (int i = 0; i < parameter_behaviours.size(); ++i) {
			auviz_parameter_behaviour *param = parameter_behaviours[i];
			if (param->on_update)
				param->on_update(param->name, param->disp_name, s, settings);
		}
	}

	attach_to_audio_source(s);
}

/* Render the audio wave source */
static void auviz_source_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	static int32_t previous_fouriered_length = 0;

	auto *s = static_cast<auviz_source *>(data);
	if (!s)
		return;

	{
		std::lock_guard<std::mutex> g(s->render_mutex);
		obs_source_process_filter_begin(s->self, GS_RGBA, OBS_NO_DIRECT_RENDERING);
		
		obs_source_t *parent = obs_filter_get_parent(s->self);
		s->width = obs_source_get_width(parent);
		s->height = obs_source_get_height(parent);

		// Rendering implementation goes here
		std::vector<float> fouriered_left = fft(s->samples_left);
		std::vector<float> fouriered_right = fft(s->samples_right);
		int32_t fouriered_length = static_cast<int32_t>(fouriered_left.size());

		//for (size_t i = 0; i < fouriered_length; i++) {
		//	obs_log(LOG_INFO, "fourier %i: %f", i, fouriered_left[i]);
		//}

		//Resize if necessary.
		if (!s->audio_freq_left_texture || previous_fouriered_length != fouriered_length) {
			if (s->audio_freq_left_texture) {
				gs_texture_destroy(s->audio_freq_left_texture);
				s->audio_freq_left_texture = nullptr;
			}
			s->audio_freq_left_texture = gs_texture_create(fouriered_length, 1, GS_R32F, 1, nullptr, GS_DYNAMIC);
		}

		uint8_t *ptr = nullptr;
		uint32_t linesize = 0;

		if (gs_texture_map(s->audio_freq_left_texture, &ptr, &linesize)) {
			memcpy(ptr, fouriered_left.data(), fouriered_length * sizeof(float));
			gs_texture_unmap(s->audio_freq_left_texture);

			gs_effect_set_texture(s->param_samples_left, s->audio_freq_left_texture);
		}

		if (gs_texture_map(s->audio_freq_right_texture, &ptr, &linesize)) {
			memcpy(ptr, fouriered_right.data(), fouriered_length * sizeof(float));
			gs_texture_unmap(s->audio_freq_right_texture);
			
			gs_effect_set_texture(s->param_samples_right, s->audio_freq_right_texture);
		}

		//obs_log(LOG_INFO, "L%f.2 R%f.2", s->samples_left.back(), s->samples_right.back());

		for (int i = 0; i < parameter_behaviours.size(); ++i) {
			auviz_parameter_behaviour *param = parameter_behaviours[i];
			if (param->on_video_render)
				param->on_video_render(param->name, param->disp_name, s);
		}

		previous_fouriered_length = fouriered_length;

		obs_source_process_filter_end(s->self, s->effect, s->width, s->height);
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

/* Add a parameter to the OBS source. 

@param name The internal name of the parameter (used for settings).
@param disp_name The display name of the parameter (shown in the UI).
@param on_create A callback function that is called when the source is created.
@param get_default A callback function that is called to get the default value of the parameter.
@param get_properties A callback function that is called to get the properties of the parameter (e.g. for dropdowns).
@param on_update A callback function that is called when the parameter is updated.
@param on_video_render A callback function that is called when the source is rendered.
*/
static void add_parameter(std::string name, std::string disp_name,
			  void (*on_create)(std::string name, std::string disp_name, auviz_source *s),
			  void (*get_default)(std::string name, std::string disp_name, obs_data_t *settings),
			  void (*get_properties)(std::string name, std::string disp_name, obs_properties_t *props,
						 auviz_source *s),
			  void (*on_update)(std::string name, std::string disp_name, auviz_source *s, obs_data_t *settings),
			  void (*on_video_render)(std::string name, std::string disp_name, auviz_source *s))
{
	auviz_parameter_behaviour *param = new auviz_parameter_behaviour();
	param->name = std::move(name);
	param->disp_name = std::move(disp_name);
	param->on_create = on_create;
	param->get_default = get_default;
	param->get_properties = get_properties;
	param->on_update = on_update;
	param->on_video_render = on_video_render;

	parameter_behaviours.push_back(param);
}

/* Compiles the effect in s->shader_filepath */
static bool compile_effect(auviz_source* s) {
	obs_enter_graphics();

	if (s->effect) {
		gs_effect_destroy(s->effect);
		s->effect = nullptr;
	}
	s->effect = gs_effect_create_from_file(s->shader_filepath.c_str(), NULL);

	if (s->effect == NULL) {
		obs_log(LOG_ERROR, "Effect compilation failed for %s", s->shader_filepath.c_str());
		obs_leave_graphics();
		return false;
	}

	// Get references to shader parameters (note: might make this a parameter from add_parameter())
	s->param_samples_left = gs_effect_get_param_by_name_ensure_success(s->effect, "samples_left");
	s->param_samples_right = gs_effect_get_param_by_name_ensure_success(s->effect, "samples_right");

	// Reset textures.
	if (s->audio_freq_left_texture) {
		gs_texture_destroy(s->audio_freq_left_texture);
		s->audio_freq_left_texture = nullptr;
	}
	if (s->audio_freq_right_texture) {
		gs_texture_destroy(s->audio_freq_right_texture);
		s->audio_freq_right_texture = nullptr;
	}
	// The 10 will likely be overwritten as the number of samples is inconsistant.
	s->audio_freq_left_texture = gs_texture_create(10, 1, GS_R32F, 1, nullptr, GS_DYNAMIC);
	s->audio_freq_right_texture = gs_texture_create(10, 1, GS_R32F, 1, nullptr, GS_DYNAMIC);

	// Call on_create for each parameter
	for (int i = 0; i < parameter_behaviours.size(); ++i) {
		auviz_parameter_behaviour *param = parameter_behaviours[i];
		if (param->on_create)
			param->on_create(param->name, param->disp_name, s);
	}
	
	obs_leave_graphics();

	obs_log(LOG_INFO, "Effect compilation succeeded for %s", s->shader_filepath.c_str());
	return true;
}

static gs_eparam_t *gs_effect_get_param_by_name_ensure_success(gs_effect_t *effect, const char *name) {
	gs_eparam_t *param = gs_effect_get_param_by_name(effect, name);
	if (!param) {
		obs_log(LOG_ERROR, "Failed to get parameter '%s' from effect", name);
	}
	return param;
}

void register_audio_viz_source() {
	std::memset(&auviz_source_info, 0, sizeof(auviz_source_info));

	auviz_source_info.id = AUVIZ_ID;
	auviz_source_info.type = OBS_SOURCE_TYPE_FILTER;
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

	//auviz_source_info.get_width = auviz_source_get_width;
	//auviz_source_info.get_height = auviz_source_get_height;

	add_parameter(
		"audio_source_name", "Audio Source Name",
		// on_create: run when source is created.
		[](std::string name, std::string disp_name, auviz_source *s) {
		},
		// get_default: sets the default value of the parameter in settings.
		[](std::string name, std::string disp_name, obs_data_t *settings) { 
			obs_data_set_default_string(settings, name.c_str(), "");
		},
		// get_properties: adds the parameter to the properties list (e.g. for dropdowns or sliders).
		[](std::string name, std::string disp_name, obs_properties_t *props, auviz_source *s) {
			obs_property_t *p_list = obs_properties_add_list(props, name.c_str(), disp_name.c_str(), OBS_COMBO_TYPE_LIST,
				OBS_COMBO_FORMAT_STRING);
			obs_enum_sources(
				[](void *data, obs_source_t *source) -> bool {
					obs_property_t *prop = (obs_property_t *)data;

					const char *id = obs_source_get_id(source);
					if (id && std::strcmp(id, AUVIZ_ID) == 0)
						return true;

					if (!obs_source_audio_active(source))
						return true;

					const char *name = obs_source_get_name(source);
					if (!name)
						return true;

					obs_property_list_add_string(prop, name, name);
					return true;
				},
				p_list);
		},
		// on_update: run when the parameter is updated (e.g. slider moved).
		[](std::string name, std::string disp_name, auviz_source *s, obs_data_t *settings) {
			s->audio_source_name = obs_data_get_string(settings, name.c_str());
		},
		// on_video_render: run when the source is rendered.
		[](std::string name, std::string disp_name, auviz_source *s) {
		});


	add_parameter(
		"movement_strength", "Strength: ",
		// on_create: run when source is created.
		[](std::string name, std::string disp_name, auviz_source *s) { 
			s->param_movement_strength = gs_effect_get_param_by_name_ensure_success(s->effect, name.c_str());
		},
		// get_default: sets the default value of the parameter in settings.
		[](std::string name, std::string disp_name, obs_data_t *settings) {
			obs_data_set_default_double(settings, name.c_str(), 0.5);
		},
		// get_properties: adds the parameter to the properties list (e.g. for dropdowns or sliders).
		[](std::string name, std::string disp_name, obs_properties_t *props, auviz_source *s) { 
			obs_properties_add_float_slider(props, name.c_str(), disp_name.c_str(), 0.0, 1.0, 0.03);
		},
		// on_update: run when the parameter is updated (e.g. slider moved).
		[](std::string name, std::string disp_name, auviz_source *s, obs_data_t *settings) {
			s->movement_strength = obs_data_get_double(settings, name.c_str());
		},
		// on_video_render: run when the source is rendered.
		[](std::string name, std::string disp_name, auviz_source *s) {
			gs_effect_set_float(s->param_movement_strength, (float)s->movement_strength);
		}
	);

	add_parameter(
		SHADER_FILEPATH_NAME, "Shader File: ",
		// on_create: run when source is created.
		[](std::string name, std::string disp_name, auviz_source *s) {
			// ON_CREATE CODE HERE
		},
		// get_default: sets the default value of the parameter in settings.
		[](std::string name, std::string disp_name, obs_data_t *settings) {
			obs_data_set_default_string(settings, name.c_str(), "");
		},
		// get_properties: adds the parameter to the properties list (e.g. for dropdowns or sliders).
		[](std::string name, std::string disp_name, obs_properties_t *props, auviz_source *s) {
			obs_properties_add_path(props, name.c_str(), disp_name.c_str(), OBS_PATH_FILE,
				 "HLSL Files (*.hlsl);;All Files (*.*)", nullptr);
		},
		// on_update: run when the parameter is updated (e.g. slider moved).
		[](std::string name, std::string disp_name, auviz_source *s, obs_data_t *settings) {
			// Recompile the shader effect if the file path has changed.
			std::string old_filepath = s->shader_filepath;
			s->shader_filepath = obs_data_get_string(settings, name.c_str());
			if (s->shader_filepath != old_filepath) {
				compile_effect(s);
			}
		},
		// on_video_render: run when the source is rendered.
		[](std::string name, std::string disp_name, auviz_source *s) {
			// ON_VIDEO_RENDER CODE HERE
		}
	);

	add_parameter(
		"width", "width",
		// on_create: run when source is created.
		[](std::string name, std::string disp_name, auviz_source *s) {
			s->param_width =
				gs_effect_get_param_by_name_ensure_success(s->effect, name.c_str());
		},
		// get_default: sets the default value of the parameter in settings.
		[](std::string name, std::string disp_name, obs_data_t *settings) {
		},
		// get_properties: adds the parameter to the properties list (e.g. for dropdowns or sliders).
		[](std::string name, std::string disp_name, obs_properties_t *props, auviz_source *s) {
		},
		// on_update: run when the parameter is updated (e.g. slider moved).
		[](std::string name, std::string disp_name, auviz_source *s, obs_data_t *settings) {
		},
		// on_video_render: run when the source is rendered.
		[](std::string name, std::string disp_name, auviz_source *s) {
			gs_effect_set_int(s->param_width, s->width);
		});

	add_parameter(
		"height", "height",
		// on_create: run when source is created.
		[](std::string name, std::string disp_name, auviz_source *s) {
			s->param_height = gs_effect_get_param_by_name_ensure_success(s->effect, name.c_str());
		},
		// get_default: sets the default value of the parameter in settings.
		[](std::string name, std::string disp_name, obs_data_t *settings) {},
		// get_properties: adds the parameter to the properties list (e.g. for dropdowns or sliders).
		[](std::string name, std::string disp_name, obs_properties_t *props, auviz_source *s) {},
		// on_update: run when the parameter is updated (e.g. slider moved).
		[](std::string name, std::string disp_name, auviz_source *s, obs_data_t *settings) {},
		// on_video_render: run when the source is rendered.
		[](std::string name, std::string disp_name, auviz_source *s) {
			gs_effect_set_int(s->param_height, s->height);
		});

	obs_register_source(&auviz_source_info);

	obs_log(LOG_INFO, "audio visualiser (%s: %s) registered.", auviz_source_info.id,
		auviz_source_info.get_name(0));
}

static size_t next_pow2(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

// Implementation of Fast Fourier Transform
static std::vector<float> fft(const std::vector<float> & samples) {
	int n = static_cast<int>(next_pow2(samples.size()));
	std::vector<std::complex<float>> complex_samples = std::vector<std::complex<float>>(n, 0.0);

	// Convert samples to complex number.
	for (int i = 0; i < samples.size(); i++) {
		complex_samples[i] = samples[i];
	}

	// Algorithm for FFT from https://cp-algorithms.com/algebra/fft.html
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;

        if (i < j)
            std::swap(complex_samples[i], complex_samples[j]);
    }

    for (int len = 2; len <= n; len <<= 1) {
        float ang = 2 * PI / len;
        std::complex<float> wlen(cos(ang), sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1);
            for (int j = 0; j < len / 2; j++) {
                std::complex<float> u = complex_samples[i+j], v = complex_samples[i+j+len/2] * w;
                complex_samples[i+j] = u + v;
                complex_samples[i+j+len/2] = u - v;
                w *= wlen;
            }
        }
    }

	// Discard imaginary part.
	std::vector<float> result = std::vector<float>(n);
	for (int i = 0; i < n; i++) {
		result[i] = complex_samples[i].real();
	}

	return result;
}

/*

USE THIS TEMPLATE FOR ADDING SOURCE PARAMETERS:

add_parameter(
	"internal_name", "Display Name",
	// on_create: run when source is created.
	[](std::string name, std::string disp_name, auviz_source *s) { 
		// ON_CREATE CODE HERE
	},
	// get_default: sets the default value of the parameter in settings.
	[](std::string name, std::string disp_name, obs_data_t *settings) {
		// GET_DEFAULT CODE HERE
	},
	// get_properties: adds the parameter to the properties list (e.g. for dropdowns or sliders).
	[](std::string name, std::string disp_name, obs_properties_t *props, auviz_source *s) {
		// GET_PROPERTIES CODE HERE
	},
	// on_update: run when the parameter is updated (e.g. slider moved).
	[](std::string name, std::string disp_name, auviz_source *s, obs_data_t *settings) {
		// ON_UPDATE CODE HERE
	},
	// on_video_render: run when the source is rendered.
	[](std::string name, std::string disp_name, auviz_source *s) {
		// ON_VIDEO_RENDER CODE HERE
	}
);

*/