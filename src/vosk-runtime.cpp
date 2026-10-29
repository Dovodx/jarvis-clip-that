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

#include "vosk-runtime.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

#ifdef _WIN32
std::wstring widen(const std::string &utf8)
{
	int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
	std::wstring out(len > 0 ? len - 1 : 0, L'\0');
	if (len > 0)
		MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), len);
	return out;
}
#endif

void *open_library(const std::string &path, std::string &error)
{
#ifdef _WIN32
	/* LOAD_WITH_ALTERED_SEARCH_PATH makes Windows resolve libvosk's own
	 * dependencies (libstdc++-6.dll etc.) from libvosk.dll's folder. */
	HMODULE mod = LoadLibraryExW(widen(path).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!mod)
		error = "LoadLibraryEx failed with error " + std::to_string(GetLastError());
	return reinterpret_cast<void *>(mod);
#else
	void *mod = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!mod) {
		const char *msg = dlerror();
		error = msg ? msg : "dlopen failed";
	}
	return mod;
#endif
}

void *find_symbol(void *handle, const char *name)
{
#ifdef _WIN32
	return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
	return dlsym(handle, name);
#endif
}

void close_library(void *handle)
{
#ifdef _WIN32
	FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
	dlclose(handle);
#endif
}

template<typename Fn> bool bind(void *handle, Fn &fn, const char *name, std::string &error)
{
	fn = reinterpret_cast<Fn>(find_symbol(handle, name));
	if (!fn)
		error = std::string("missing symbol ") + name;
	return fn != nullptr;
}

} // namespace

bool VoskRuntime::load(const std::string &path, std::string &error)
{
	unload();

	handle = open_library(path, error);
	if (!handle)
		return false;

	bool ok = bind(handle, model_new, "vosk_model_new", error) &&
		  bind(handle, model_free, "vosk_model_free", error) &&
		  bind(handle, recognizer_new_grm, "vosk_recognizer_new_grm", error) &&
		  bind(handle, recognizer_free, "vosk_recognizer_free", error) &&
		  bind(handle, recognizer_set_words, "vosk_recognizer_set_words", error) &&
		  bind(handle, recognizer_accept_waveform_s, "vosk_recognizer_accept_waveform_s", error) &&
		  bind(handle, recognizer_result, "vosk_recognizer_result", error) &&
		  bind(handle, recognizer_final_result, "vosk_recognizer_final_result", error) &&
		  bind(handle, recognizer_reset, "vosk_recognizer_reset", error) &&
		  bind(handle, set_log_level, "vosk_set_log_level", error);

	if (!ok)
		unload();
	return ok;
}

void VoskRuntime::unload()
{
	if (handle)
		close_library(handle);
	handle = nullptr;
	model_new = nullptr;
	model_free = nullptr;
	recognizer_new_grm = nullptr;
	recognizer_free = nullptr;
	recognizer_set_words = nullptr;
	recognizer_accept_waveform_s = nullptr;
	recognizer_result = nullptr;
	recognizer_final_result = nullptr;
	recognizer_reset = nullptr;
	set_log_level = nullptr;
}
