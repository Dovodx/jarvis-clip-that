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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vosk-runtime.h"

/* Sample rate the recognizer is fed with (mono, signed 16-bit). */
constexpr int DETECTOR_SAMPLE_RATE = 16000;

/* "Jarvis, clip that!" -> {"jarvis", "clip", "that"} */
std::vector<std::string> normalize_phrase(const std::string &phrase);

/*
 * Returns true if `result_json` (a Vosk final result with word details)
 * contains `phrase` as consecutive words, each recognized with at least
 * `min_confidence`. `heard` receives the full recognized text.
 */
bool result_contains_phrase(const char *result_json, const std::vector<std::string> &phrase, float min_confidence,
			    std::string *heard);

/*
 * Wraps a Vosk recognizer restricted to a grammar of just the wake phrase plus
 * "[unk]". Constraining the grammar keeps CPU usage low and means anything
 * that is not the wake phrase is absorbed by the unknown-word token instead of
 * being force-fitted onto it.
 */
class PhraseDetector {
public:
	PhraseDetector() = default;
	PhraseDetector(const PhraseDetector &) = delete;
	PhraseDetector &operator=(const PhraseDetector &) = delete;
	~PhraseDetector();

	bool init(VoskRuntime *vosk, VoskModel *model, const std::string &phrase, float min_confidence,
		  std::string &error);

	/* Feed 16 kHz mono samples. Returns true when an utterance just ended
	 * and contained the wake phrase. */
	bool feed(const int16_t *samples, size_t count, std::string *heard = nullptr);

	/* Force the current utterance to end (e.g. at end of a test file). */
	bool flush(std::string *heard = nullptr);

	void reset();

private:
	VoskRuntime *vosk = nullptr;
	VoskRecognizer *recognizer = nullptr;
	std::vector<std::string> phrase_words;
	float min_confidence = 0.0f;
};
