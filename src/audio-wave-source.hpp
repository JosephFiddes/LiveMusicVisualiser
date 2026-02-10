#pragma once

#include <obs-module.h>
#include <plugin-support.h>

#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <cstdint>

struct auviz_source;

struct auviz_source {
	obs_source_t *self = nullptr;

	std::string name;
	obs_weak_source_t *audio_weak = nullptr;

	// Lifetime guards for audio callback (prevents use-after-free during destroy)
	std::atomic<bool> alive{true};
	std::atomic<uint32_t> audio_cb_inflight{0};

	std::mutex audio_mutex;
	std::vector<float> samples_left;
	std::vector<float> samples_right;
	size_t num_samples = 0;

	std::vector<float> wave;

	// Core visual parameters
	int width = 800;
	int height = 200;

	// Render-state guard (prevents update() vs video_render() races)
	std::mutex render_mutex;
};

void register_audio_viz_source(void);