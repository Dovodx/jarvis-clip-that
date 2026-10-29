/*
Jarvis Clip That
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "voice-listener.h"
#include "phrase-detector.h"
#include "vosk-runtime.h"

#include <obs-module.h>
#include <plugin-support.h>

#include <miniaudio.h>

#include <algorithm>
#include <cctype>
#include <chrono>

namespace {

/* Drop audio rather than grow without bound if recognition falls behind. */
constexpr size_t MAX_PENDING_SAMPLES = DETECTOR_SAMPLE_RATE * 5;

void capture_callback(ma_device *device, void *, const void *input, ma_uint32 frame_count)
{
	auto *listener = static_cast<VoiceListener *>(device->pUserData);
	listener->push_samples(static_cast<const int16_t *>(input), frame_count);
}

bool contains_ignore_case(const std::string &haystack, const std::string &needle)
{
	auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), [](char a, char b) {
		return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
	});
	return it != haystack.end();
}

/* Finds a capture device whose name contains `name`. Returns false (and logs
 * the available devices) if none matches. */
bool find_capture_device(ma_context *context, const std::string &name, ma_device_id &id)
{
	ma_device_info *devices = nullptr;
	ma_uint32 count = 0;

	if (ma_context_get_devices(context, nullptr, nullptr, &devices, &count) != MA_SUCCESS)
		return false;

	for (ma_uint32 i = 0; i < count; i++) {
		if (contains_ignore_case(devices[i].name, name)) {
			id = devices[i].id;
			return true;
		}
	}

	obs_log(LOG_WARNING, "No microphone matching \"%s\". Available capture devices:", name.c_str());
	for (ma_uint32 i = 0; i < count; i++)
		obs_log(LOG_WARNING, "  - %s", devices[i].name);
	return false;
}

} // namespace

VoiceListener::VoiceListener(TriggerFn on_trigger_) : on_trigger(std::move(on_trigger_)) {}

VoiceListener::~VoiceListener()
{
	stop();
}

void VoiceListener::start(const ListenerConfig &config_)
{
	stop();
	config = config_;
	quit = false;
	state_changed = true;
	worker = std::thread(&VoiceListener::run, this);
}

void VoiceListener::stop()
{
	if (!worker.joinable())
		return;

	{
		std::lock_guard<std::mutex> lock(mutex);
		quit = true;
	}
	cv.notify_all();
	worker.join();
}

void VoiceListener::set_active(bool active)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (want_active == active)
			return;
		want_active = active;
		state_changed = true;
	}
	cv.notify_all();
}

void VoiceListener::push_samples(const int16_t *samples, size_t count)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (pending.size() + count > MAX_PENDING_SAMPLES)
			return;
		pending.insert(pending.end(), samples, samples + count);
	}
	cv.notify_one();
}

void VoiceListener::run()
{
	std::string error;

	VoskRuntime vosk;
	if (!vosk.load(config.vosk_library, error)) {
		obs_log(LOG_ERROR, "Could not load Vosk from %s: %s", config.vosk_library.c_str(), error.c_str());
		return;
	}
	vosk.set_log_level(-1);

	obs_log(LOG_INFO, "Loading speech model from %s", config.model_path.c_str());
	VoskModel *model = vosk.model_new(config.model_path.c_str());
	if (!model) {
		obs_log(LOG_ERROR, "Could not load speech model from %s", config.model_path.c_str());
		return;
	}

	/* Scoped so the detector (and its recognizer) is destroyed before the
	 * model and library it depends on. */
	{
		PhraseDetector detector;
		if (!detector.init(&vosk, model, config.phrase, config.min_confidence, error)) {
			obs_log(LOG_ERROR, "Could not set up phrase detection: %s", error.c_str());
			vosk.model_free(model);
			return;
		}

		ma_context context;
		if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
			obs_log(LOG_ERROR, "Could not initialize audio capture");
			vosk.model_free(model);
			return;
		}
		obs_log(LOG_INFO, "Audio backend: %s", ma_get_backend_name(context.backend));

		ma_device device;
		bool device_open = false;
		auto last_trigger = std::chrono::steady_clock::time_point();
		std::vector<int16_t> chunk;
		std::string heard;

		obs_log(LOG_INFO, "Ready. Listening for \"%s\" while the replay buffer is running",
			config.phrase.c_str());

		std::unique_lock<std::mutex> lock(mutex);
		while (!quit) {
			cv.wait(lock, [&] { return quit || state_changed || !pending.empty(); });
			if (quit)
				break;

			bool want = want_active;
			bool changed = state_changed;
			state_changed = false;
			chunk.swap(pending);
			pending.clear();
			lock.unlock();

			if (changed && want && !device_open) {
				ma_device_config dc = ma_device_config_init(ma_device_type_capture);
				dc.capture.format = ma_format_s16;
				dc.capture.channels = 1;
				dc.sampleRate = DETECTOR_SAMPLE_RATE;
				dc.dataCallback = capture_callback;
				dc.pUserData = this;

				ma_device_id id;
				if (!config.device.empty() && find_capture_device(&context, config.device, id))
					dc.capture.pDeviceID = &id;

				if (ma_device_init(&context, &dc, &device) != MA_SUCCESS) {
					obs_log(LOG_ERROR, "Could not open microphone");
				} else if (ma_device_start(&device) != MA_SUCCESS) {
					obs_log(LOG_ERROR, "Could not start microphone capture");
					ma_device_uninit(&device);
				} else {
					device_open = true;
					obs_log(LOG_INFO, "Listening on microphone: %s", device.capture.name);
				}
			} else if (changed && !want && device_open) {
				ma_device_uninit(&device);
				device_open = false;
				detector.reset();
				obs_log(LOG_INFO, "Stopped listening");
			}

			if (device_open && !chunk.empty() && detector.feed(chunk.data(), chunk.size(), &heard)) {
				auto now = std::chrono::steady_clock::now();
				if (now - last_trigger >= std::chrono::milliseconds(config.cooldown_ms)) {
					last_trigger = now;
					obs_log(LOG_INFO, "Wake phrase detected (heard \"%s\")", heard.c_str());
					on_trigger(heard);
				}
			}
			chunk.clear();

			lock.lock();
		}
		lock.unlock();

		if (device_open)
			ma_device_uninit(&device);
		ma_context_uninit(&context);

		std::lock_guard<std::mutex> guard(mutex);
		pending.clear();
	}

	vosk.model_free(model);
}
