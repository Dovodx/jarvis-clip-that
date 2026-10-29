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

#include "phrase-detector.h"

#include <obs-data.h>

#include <algorithm>
#include <cctype>

/*
 * With only the wake phrase and [unk] to choose from, the decoder happily maps
 * sound-alikes ("hey Travis, flip that") onto the wake phrase with high
 * confidence. Offering similar-sounding alternatives gives those utterances
 * somewhere else to go.
 */
static const char *const DECOY_WORDS[] = {
	"travis", "harvest", "service", "nervous", "jar",  "jars", "jasper",  "garbage", "marvin", "davis",
	"flip",   "slip",    "ship",    "chip",    "trip", "skip", "clipped", "clips",   "lip",    "click",
	"cut",    "that",    "this",    "it",      "the",  "hat",  "cat",     "at",      "hey",    "okay",
};

std::vector<std::string> normalize_phrase(const std::string &phrase)
{
	std::vector<std::string> words;
	std::string word;

	for (unsigned char c : phrase) {
		if (std::isalnum(c) || c == '\'') {
			word += static_cast<char>(std::tolower(c));
		} else if (!word.empty()) {
			words.push_back(word);
			word.clear();
		}
	}
	if (!word.empty())
		words.push_back(word);
	return words;
}

bool result_contains_phrase(const char *result_json, const std::vector<std::string> &phrase, float min_confidence,
			    std::string *heard)
{
	if (!result_json || phrase.empty())
		return false;

	obs_data_t *result = obs_data_create_from_json(result_json);
	if (!result)
		return false;

	if (heard)
		*heard = obs_data_get_string(result, "text");

	std::vector<std::string> words;
	std::vector<double> confidences;

	obs_data_array_t *items = obs_data_get_array(result, "result");
	size_t count = obs_data_array_count(items);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *item = obs_data_array_item(items, i);
		words.emplace_back(obs_data_get_string(item, "word"));
		confidences.push_back(obs_data_get_double(item, "conf"));
		obs_data_release(item);
	}
	obs_data_array_release(items);
	obs_data_release(result);

	if (words.size() < phrase.size())
		return false;

	for (size_t start = 0; start + phrase.size() <= words.size(); start++) {
		bool match = true;
		for (size_t j = 0; j < phrase.size() && match; j++) {
			match = words[start + j] == phrase[j] && confidences[start + j] >= min_confidence;
		}
		if (match)
			return true;
	}
	return false;
}

PhraseDetector::~PhraseDetector()
{
	if (recognizer)
		vosk->recognizer_free(recognizer);
}

bool PhraseDetector::init(VoskRuntime *vosk_, VoskModel *model, const std::string &phrase, float min_confidence_,
			  std::string &error)
{
	phrase_words = normalize_phrase(phrase);
	if (phrase_words.empty()) {
		error = "wake phrase is empty";
		return false;
	}

	std::string joined;
	for (const std::string &w : phrase_words)
		joined += (joined.empty() ? "" : " ") + w;

	/* normalize_phrase only keeps [a-z0-9'], so no JSON escaping needed */
	std::string grammar = "[\"" + joined + "\"";
	for (const char *decoy : DECOY_WORDS)
		grammar += std::string(", \"") + decoy + "\"";
	grammar += ", \"[unk]\"]";

	vosk = vosk_;
	min_confidence = min_confidence_;
	recognizer = vosk->recognizer_new_grm(model, static_cast<float>(DETECTOR_SAMPLE_RATE), grammar.c_str());
	if (!recognizer) {
		error = "failed to create recognizer for grammar " + grammar;
		return false;
	}
	vosk->recognizer_set_words(recognizer, 1);
	return true;
}

bool PhraseDetector::feed(const int16_t *samples, size_t count, std::string *heard)
{
	bool detected = false;

	/* Vosk only checks for an utterance endpoint at the end of each call,
	 * so feed in ~100 ms slices to pick up results promptly. */
	constexpr size_t slice = DETECTOR_SAMPLE_RATE / 10;

	while (count > 0) {
		int n = static_cast<int>(std::min(count, slice));
		if (vosk->recognizer_accept_waveform_s(recognizer, samples, n) == 1) {
			detected |= result_contains_phrase(vosk->recognizer_result(recognizer), phrase_words,
							   min_confidence, heard);
		}
		samples += n;
		count -= static_cast<size_t>(n);
	}
	return detected;
}

bool PhraseDetector::flush(std::string *heard)
{
	return result_contains_phrase(vosk->recognizer_final_result(recognizer), phrase_words, min_confidence, heard);
}

void PhraseDetector::reset()
{
	vosk->recognizer_reset(recognizer);
}
