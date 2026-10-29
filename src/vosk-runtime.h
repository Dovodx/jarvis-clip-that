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

#include <string>

/*
 * libvosk is loaded at runtime from the plugin's data directory instead of
 * being linked. That way OBS can still load the plugin (and report a useful
 * error) if the library is missing, and on Windows the MinGW runtime DLLs that
 * ship with libvosk.dll are resolved from the same folder.
 */

struct VoskModel;
struct VoskRecognizer;

struct VoskRuntime {
	VoskModel *(*model_new)(const char *model_path) = nullptr;
	void (*model_free)(VoskModel *model) = nullptr;
	VoskRecognizer *(*recognizer_new_grm)(VoskModel *model, float sample_rate, const char *grammar) = nullptr;
	void (*recognizer_free)(VoskRecognizer *recognizer) = nullptr;
	void (*recognizer_set_words)(VoskRecognizer *recognizer, int words) = nullptr;
	int (*recognizer_accept_waveform_s)(VoskRecognizer *recognizer, const short *data, int length) = nullptr;
	const char *(*recognizer_result)(VoskRecognizer *recognizer) = nullptr;
	const char *(*recognizer_final_result)(VoskRecognizer *recognizer) = nullptr;
	void (*recognizer_reset)(VoskRecognizer *recognizer) = nullptr;
	void (*set_log_level)(int log_level) = nullptr;

	/* Loads every symbol above from the library at `path`. On failure,
	 * `error` describes what went wrong and the runtime stays unloaded. */
	bool load(const std::string &path, std::string &error);
	void unload();
	bool loaded() const { return handle != nullptr; }

	~VoskRuntime() { unload(); }

private:
	void *handle = nullptr;
};
