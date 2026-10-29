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

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <plugin-support.h>

#include "voice-listener.h"

#include <atomic>
#include <initializer_list>
#include <memory>
#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#ifdef _WIN32
#define VOSK_LIBRARY "vosk/libvosk.dll"
#else
#define VOSK_LIBRARY "vosk/libvosk.so"
#endif

namespace {

std::unique_ptr<VoiceListener> listener;
std::atomic<bool> enabled{true};
std::atomic<bool> replay_active{false};
obs_hotkey_id toggle_hotkey = OBS_INVALID_HOTKEY_ID;
obs_data_t *config = nullptr;

std::string module_file(const char *file)
{
	char *path = obs_module_file(file);
	std::string result = path ? path : "";
	bfree(path);
	return result;
}

std::string config_file_path()
{
	char *path = obs_module_config_path("config.json");
	std::string result = path ? path : "";
	bfree(path);
	return result;
}

obs_data_t *load_config()
{
	obs_data_t *data = obs_data_create_from_json_file_safe(config_file_path().c_str(), "bak");
	if (!data)
		data = obs_data_create();

	obs_data_set_default_bool(data, "enabled", true);
	obs_data_set_default_string(data, "phrase", "Jarvis, clip that");
	obs_data_set_default_double(data, "min_confidence", 0.6);
	obs_data_set_default_int(data, "cooldown_ms", 3000);
	obs_data_set_default_string(data, "microphone", "");
	obs_data_set_default_string(data, "model_path", "");

	/* Write every option out explicitly so users can find and edit them.
	 * Strings are copied first: setting an item reallocates it, which would
	 * invalidate a pointer returned by obs_data_get_string for that item. */
	obs_data_set_bool(data, "enabled", obs_data_get_bool(data, "enabled"));
	obs_data_set_double(data, "min_confidence", obs_data_get_double(data, "min_confidence"));
	obs_data_set_int(data, "cooldown_ms", obs_data_get_int(data, "cooldown_ms"));
	for (const char *key : {"phrase", "microphone", "model_path"}) {
		std::string value = obs_data_get_string(data, key);
		obs_data_set_string(data, key, value.c_str());
	}
	return data;
}

void save_config()
{
	if (!config)
		return;

	char *dir = obs_module_config_path("");
	os_mkdirs(dir);
	bfree(dir);

	obs_data_set_bool(config, "enabled", enabled);
	if (toggle_hotkey != OBS_INVALID_HOTKEY_ID) {
		obs_data_array_t *hotkey = obs_hotkey_save(toggle_hotkey);
		obs_data_set_array(config, "toggle_hotkey", hotkey);
		obs_data_array_release(hotkey);
	}
	obs_data_save_json_safe(config, config_file_path().c_str(), "tmp", "bak");
}

void update_listener()
{
	if (listener)
		listener->set_active(enabled && replay_active);
}

void set_enabled(bool value)
{
	enabled = value;
	obs_log(LOG_INFO, "Voice activation %s", value ? "enabled" : "disabled");
	update_listener();
}

void save_replay(void *)
{
	if (!obs_frontend_replay_buffer_active()) {
		obs_log(LOG_WARNING, "Wake phrase heard but the replay buffer is not running");
		return;
	}
	obs_frontend_replay_buffer_save();
}

void on_toggle_hotkey(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		set_enabled(!enabled);
}

void on_tools_menu(void *)
{
	set_enabled(!enabled);
}

void on_frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		replay_active = obs_frontend_replay_buffer_active();
		update_listener();
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
		replay_active = true;
		update_listener();
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
		replay_active = false;
		update_listener();
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED: {
		char *path = obs_frontend_get_last_replay();
		obs_log(LOG_INFO, "Clip saved: %s", path ? path : "(unknown)");
		bfree(path);
		break;
	}
	case OBS_FRONTEND_EVENT_EXIT:
		/* Hotkeys and the frontend are torn down before module unload. */
		save_config();
		if (listener)
			listener->stop();
		obs_hotkey_unregister(toggle_hotkey);
		toggle_hotkey = OBS_INVALID_HOTKEY_ID;
		break;
	default:
		break;
	}
}

} // namespace

bool obs_module_load(void)
{
	config = load_config();
	enabled = obs_data_get_bool(config, "enabled");

	ListenerConfig lc;
	lc.vosk_library = module_file(VOSK_LIBRARY);
	lc.model_path = obs_data_get_string(config, "model_path");
	if (lc.model_path.empty())
		lc.model_path = module_file("model");
	lc.phrase = obs_data_get_string(config, "phrase");
	lc.device = obs_data_get_string(config, "microphone");
	lc.min_confidence = static_cast<float>(obs_data_get_double(config, "min_confidence"));
	lc.cooldown_ms = static_cast<int>(obs_data_get_int(config, "cooldown_ms"));

	if (lc.vosk_library.empty()) {
		obs_log(LOG_ERROR, "Missing %s in the plugin data folder", VOSK_LIBRARY);
		return false;
	}
	if (lc.model_path.empty()) {
		obs_log(LOG_ERROR, "Missing speech model in the plugin data folder");
		return false;
	}

	toggle_hotkey = obs_hotkey_register_frontend("jarvis_clip_that.toggle", obs_module_text("ToggleListening"),
						     on_toggle_hotkey, nullptr);
	obs_data_array_t *hotkey = obs_data_get_array(config, "toggle_hotkey");
	obs_hotkey_load(toggle_hotkey, hotkey);
	obs_data_array_release(hotkey);

	obs_frontend_add_tools_menu_item(obs_module_text("ToggleListening"), on_tools_menu, nullptr);
	obs_frontend_add_event_callback(on_frontend_event, nullptr);

	listener = std::make_unique<VoiceListener>(
		[](const std::string &) { obs_queue_task(OBS_TASK_UI, save_replay, nullptr, false); });
	listener->start(lc);

	save_config();
	obs_log(LOG_INFO, "Plugin loaded (version %s). Config: %s", PLUGIN_VERSION, config_file_path().c_str());
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	listener.reset();
	obs_data_release(config);
	config = nullptr;
	obs_log(LOG_INFO, "Plugin unloaded");
}

const char *obs_module_description(void)
{
	return obs_module_text("Description");
}
