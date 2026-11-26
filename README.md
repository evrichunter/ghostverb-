# GHOST VERB
**Reverse Reverb Generator - designed by Evric Hunter - FREE VST3**

GHOST VERB captures a piece of audio, runs it through a *real* reverse reverb and gives you a wet-only WAV that
builds up and ends exactly on the attack of the selected sound.

How the effect is made (all offline, on a background thread):

1. extract the selected fragment, 2. reverse it, 3. stereo algorithmic reverb 100 % wet (8-line FDN),
4. render the complete reverb tail, 5. reverse everything again, 6. keep the wet swell: exactly `LENGTH` long,
ending on the attack (the reverb's own pre-delay is removed so the peak of the swell lands on the attack).

## Install in FL Studio (Windows x64)
1. Download the artifact **GHOST-VERB-1.0.0-Windows-x64-VST3** from the *Actions* tab (latest green run) and unzip it.
2. Copy the folder **`GHOST VERB.vst3`** to `C:\Program Files\Common Files\VST3` (admin rights required).
3. FL Studio -> *Options -> Manage plugins -> Find installed plugins* (verify "Scan in 'Plugin search paths'" includes the folder above).
4. Add **GHOST VERB** to a mixer insert slot (it appears under *Fx*).

## Use
1. Press **CAPTURE** and play the audio in FL Studio (the playlist must be playing so FL feeds the insert). **STOP** ends it
   (it also stops by itself at the limit - 20 s by default, right-click CAPTURE to choose 5 / 10 / 20 / 30 s).
   The audio always passes through untouched.
2. Drag the two markers on the waveform. **ATTACK** (left) is the moment where the reverse reverb must land;
   **END** (right) closes the fragment. **LISTEN** auditions the selection.
3. Choose **LENGTH** (1/4, 1/2 or 1 bar - taken from the host tempo and time signature, 120 BPM 4/4 if the host gives none),
   then **SIZE** (room size + decay), **TONE** (dark - bright), **WIDTH** (0 - 200 %) and **OUTPUT**.
4. **GENERATE** renders in the background (FL keeps playing). **PREVIEW** plays the result through the insert.
5. Drag the **DRAG WAV** box into the playlist (or a Sampler channel) and place the file so that its **end** sits on the
   attack. Or use **EXPORT WAV** (24-bit stereo) at any time.

Notes: PREVIEW/LISTEN are mixed on top of the insert's signal, so FL has to be processing the track (play the playlist).
Capture and generated audio are not saved in the project; the control values are.

## Build
Requirements: CMake >= 3.22, Visual Studio 2022 (Windows) - JUCE 8.0.6 is downloaded automatically.
```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target GhostVerb_VST3
build\GhostVerbTests_artefacts\Release\GhostVerbTests.exe      # tests (cmake --build build --config Release --target GhostVerbTests)
```
The VST3 is created in `build\GhostVerb_artefacts\Release\VST3\GHOST VERB.vst3`.
GitHub Actions (`.github/workflows/build.yml`) does this on every push and uploads the VST3 as an artifact.

## Layout
`PluginProcessor` (audio thread, APVTS, host tempo) - `PluginEditor` + `UIComponents` (native JUCE drawing) -
`AudioCaptureEngine` (real-time-safe recorder) - `ReverseReverbEngine` (FDN reverb + reverse chain) -
`AudioRenderer` (render thread, WAV 24-bit) - `PreviewPlayer`.

## License
AGPLv3 (JUCE 8 is used under its AGPLv3 option). See `LICENSE` and `THIRD_PARTY_NOTICES.md`.
