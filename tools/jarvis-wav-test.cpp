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

/*
 * Runs the wake phrase detector over a 16 kHz mono 16-bit WAV file and prints
 * every detection. Exit status is 0 if the phrase was detected at least once.
 *
 *   jarvis-wav-test file.wav ["phrase"] [min_confidence]
 */

#include "phrase-detector.h"
#include "vosk-runtime.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

static bool read_wav(const char *path, std::vector<int16_t> &samples)
{
	std::ifstream in(path, std::ios::binary);
	char riff[12];
	if (!in.read(riff, 12) || std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0)
		return false;

	uint16_t format = 0, channels = 0, bits = 0;
	uint32_t rate = 0;
	char id[4];
	uint32_t size;

	while (in.read(id, 4) && in.read(reinterpret_cast<char *>(&size), 4)) {
		if (std::memcmp(id, "fmt ", 4) == 0) {
			std::vector<char> fmt(size);
			in.read(fmt.data(), size);
			std::memcpy(&format, &fmt[0], 2);
			std::memcpy(&channels, &fmt[2], 2);
			std::memcpy(&rate, &fmt[4], 4);
			std::memcpy(&bits, &fmt[14], 2);
		} else if (std::memcmp(id, "data", 4) == 0) {
			if (format != 1 || channels != 1 || bits != 16 || rate != DETECTOR_SAMPLE_RATE) {
				std::fprintf(stderr, "need 16 kHz mono 16-bit PCM (got fmt=%u ch=%u bits=%u rate=%u)\n",
					     format, channels, bits, rate);
				return false;
			}
			samples.resize(size / 2);
			in.read(reinterpret_cast<char *>(samples.data()), size);
			return true;
		} else {
			in.seekg(size + (size & 1), std::ios::cur);
		}
	}
	return false;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s file.wav [\"phrase\"] [min_confidence]\n", argv[0]);
		return 2;
	}
	const char *phrase = argc > 2 ? argv[2] : "Jarvis, clip that";
	float min_confidence = argc > 3 ? std::strtof(argv[3], nullptr) : 0.6f;
	const char *lib = std::getenv("VOSK_LIBRARY") ? std::getenv("VOSK_LIBRARY") : DEFAULT_VOSK_LIBRARY;
	const char *model_path = std::getenv("VOSK_MODEL") ? std::getenv("VOSK_MODEL") : DEFAULT_MODEL_PATH;

	std::vector<int16_t> samples;
	if (!read_wav(argv[1], samples)) {
		std::fprintf(stderr, "could not read %s\n", argv[1]);
		return 2;
	}

	std::string error;
	VoskRuntime vosk;
	if (!vosk.load(lib, error)) {
		std::fprintf(stderr, "could not load %s: %s\n", lib, error.c_str());
		return 2;
	}
	vosk.set_log_level(-1);

	VoskModel *model = vosk.model_new(model_path);
	if (!model) {
		std::fprintf(stderr, "could not load model %s\n", model_path);
		return 2;
	}

	int detections = 0;
	{
		PhraseDetector detector;
		if (!detector.init(&vosk, model, phrase, min_confidence, error)) {
			std::fprintf(stderr, "%s\n", error.c_str());
			return 2;
		}

		/* Feed in 20 ms blocks like a live capture device would. */
		std::string heard;
		const size_t block = DETECTOR_SAMPLE_RATE / 50;
		for (size_t pos = 0; pos < samples.size(); pos += block) {
			size_t n = std::min(block, samples.size() - pos);
			if (detector.feed(samples.data() + pos, n, &heard)) {
				std::printf("DETECTED at %.2fs (heard \"%s\")\n",
					    double(pos + n) / DETECTOR_SAMPLE_RATE, heard.c_str());
				detections++;
			}
		}
		if (detector.flush(&heard)) {
			std::printf("DETECTED at end (heard \"%s\")\n", heard.c_str());
			detections++;
		}
	}
	vosk.model_free(model);

	std::printf("%d detection(s)\n", detections);
	return detections > 0 ? 0 : 1;
}
