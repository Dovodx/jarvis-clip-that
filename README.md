# Jarvis Clip That

An OBS Studio plugin that saves your replay buffer when you say **"Jarvis, clip that"**.

- Speech recognition runs **offline** with [Vosk](https://alphacephei.com/vosk/) and its small English model. No audio leaves your machine.
- The recognizer only knows the wake phrase, a few sound-alike decoy words, and an "unknown" token. That keeps CPU use low, and ordinary conversation, "Jarvis, what time is it", or "hey Travis, flip that" won't trigger it.
- It captures the **system default microphone** through [miniaudio](https://miniaud.io) (WASAPI on Windows, PulseAudio/PipeWire or ALSA on Linux). If you change the default device, it follows the change.
- The mic is only opened **while the replay buffer is running**.
- Supported platforms: **Windows x64** and **Linux x86_64**, OBS 31 or newer.

## Usage

1. Install the plugin (see below) and restart OBS.
2. Enable the replay buffer: **Settings → Output → Replay Buffer**.
3. Start the replay buffer.
4. Say "Jarvis, clip that". The clip is saved the same way as pressing the "Save Replay" hotkey. The OBS log (**Help → Log Files**) shows `Wake phrase detected` and `Clip saved: <path>`.

To toggle voice activation, use **Tools → Jarvis Clip That: Toggle Voice Activation**. You can also bind a hotkey for it under **Settings → Hotkeys**.

### Configuration

The plugin creates a config file the first time it runs. Edit it while OBS is closed:

| Platform | Path |
| --- | --- |
| Windows | `%APPDATA%\obs-studio\plugin_config\jarvis-clip-that\config.json` |
| Linux | `~/.config/obs-studio/plugin_config/jarvis-clip-that/config.json` |

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `true` | Whether voice activation is on (the Tools menu and hotkey also change this) |
| `phrase` | `"Jarvis, clip that"` | Wake phrase. Each word has to be in the model's vocabulary. |
| `min_confidence` | `0.6` | Minimum per-word confidence (0–1). Raise it if you get false triggers, lower it if real ones get missed. |
| `cooldown_ms` | `3000` | Ignore repeat detections for this long after a clip |
| `microphone` | `""` | Empty uses the system default mic. Otherwise, part of a device name, e.g. `"SoloCast"`. If nothing matches, the log lists the available devices. |
| `model_path` | `""` | Empty uses the bundled model. You can point this at another unpacked Vosk model, e.g. the larger `vosk-model-en-us-0.22-lgraph`, for better accuracy. |

## Installing

### Linux

Build it (see below), then copy it into your per-user OBS plugin folder:

```bash
P=~/.config/obs-studio/plugins/jarvis-clip-that
mkdir -p "$P/bin/64bit" "$P/data"
cp build_x86_64/rundir/RelWithDebInfo/jarvis-clip-that.so "$P/bin/64bit/"
cp -r build_x86_64/rundir/RelWithDebInfo/jarvis-clip-that/. "$P/data/"
```

For the Flatpak version of OBS, use `~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/` instead. This path hasn't been tested.

### Windows

Unzip the Windows build into `C:\ProgramData\obs-studio\plugins\` so you end up with:

```
C:\ProgramData\obs-studio\plugins\jarvis-clip-that\
├── bin\64bit\jarvis-clip-that.dll
└── data\
    ├── locale\en-US.ini
    ├── model\…
    └── vosk\libvosk.dll (+ libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll)
```

If Windows blocks microphone access, enable **Settings → Privacy & security → Microphone → Let desktop apps access your microphone**.

## Building

CMake downloads the dependencies at configure time and checks their SHA-256 hashes: the prebuilt Vosk 0.3.45 runtime, `vosk-model-small-en-us-0.15` (about 40 MB), and miniaudio 0.11.25.

### Linux

You need CMake ≥ 3.28, Ninja, a C++17 compiler, and the OBS development files (`obs-studio` on Arch, which includes them; `libobs-dev` from the OBS PPA on Ubuntu).

```bash
cmake --preset ubuntu-x86_64
cmake --build build_x86_64
```

### Windows

The project is based on the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate). The template's build scripts download the matching OBS sources and obs-deps, then build `libobs` and `obs-frontend-api` for you. You need Visual Studio 2022 (with the C++ workload) and CMake ≥ 3.28.

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
cmake --install build_x64 --prefix release --config RelWithDebInfo
```

`release\jarvis-clip-that\` is then ready to copy into `C:\ProgramData\obs-studio\plugins\`.

### GitHub Actions

If you push this repository to GitHub, the included workflows build Windows and Ubuntu packages on every push. You can download them from the workflow run's artifacts. Pushing a tag like `1.0.0` creates a draft release.

### Testing detection without OBS

```bash
cmake --preset ubuntu-x86_64 -DBUILD_WAV_TEST=ON
cmake --build build_x86_64
build_x86_64/jarvis-wav-test recording.wav                    # 16 kHz mono 16-bit WAV
build_x86_64/jarvis-wav-test recording.wav "Jarvis, clip that" 0.7
```

To convert any recording first: `ffmpeg -i in.m4a -ar 16000 -ac 1 -c:a pcm_s16le recording.wav`.

## How it works

```
mic ─▶ miniaudio (16 kHz mono s16) ─▶ worker thread ─▶ Vosk grammar recognizer
                                                         │ utterance ends
                                                         ▼
                              words "jarvis clip that", each conf ≥ min_confidence?
                                                         │ yes (and not in cooldown)
                                                         ▼
                              obs_queue_task(UI) ─▶ obs_frontend_replay_buffer_save()
```

- `src/plugin-main.cpp`: module entry point, config, hotkey, Tools menu, and replay buffer events.
- `src/voice-listener.cpp`: opens and closes the microphone and runs the recognition thread.
- `src/phrase-detector.cpp`: Vosk grammar setup and phrase matching.
- `src/vosk-runtime.cpp`: loads `libvosk` at runtime from the plugin's data folder.

Detection fires when the utterance ends, i.e. after a short pause, roughly 0.5–1 s after you finish speaking. That delay doesn't matter for the clip because the replay buffer already holds the past N seconds.

## License

GPL-2.0-or-later (same as the OBS plugin template). Vosk and its models are Apache-2.0, and miniaudio is public domain / MIT-0.
