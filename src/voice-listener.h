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

#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ListenerConfig {
	std::string vosk_library; /* path to libvosk.so / libvosk.dll */
	std::string model_path;   /* path to an unpacked Vosk model directory */
	std::string phrase;       /* wake phrase, e.g. "Jarvis, clip that" */
	std::string device;       /* capture device name substring, empty = system default */
	float min_confidence = 0.6f;
	int cooldown_ms = 3000;
};

/*
 * Captures the microphone on a background thread and invokes the trigger
 * callback (on that thread) whenever the wake phrase is recognized. The mic is
 * only opened while set_active(true) is in effect, so it is not held open when
 * there is no replay buffer to save.
 */
class VoiceListener {
public:
	using TriggerFn = std::function<void(const std::string &heard)>;

	explicit VoiceListener(TriggerFn on_trigger);
	~VoiceListener();

	void start(const ListenerConfig &config);
	void stop();
	void set_active(bool active);

	/* Called from the audio device thread. */
	void push_samples(const int16_t *samples, size_t count);

private:
	void run();

	TriggerFn on_trigger;
	ListenerConfig config;
	std::thread worker;

	std::mutex mutex;
	std::condition_variable cv;
	std::vector<int16_t> pending;
	bool quit = false;
	bool want_active = false;
	bool state_changed = false;
};
