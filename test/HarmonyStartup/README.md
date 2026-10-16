# Audio startup tests

From the repository root, run:

```powershell
./test/HarmonyStartup/run-tests.ps1
```

The runner uses the project-local Qt 6.12 host toolchain, DevEco CMake/Ninja, and one build job. Build outputs and caches stay under the project `tmp/` directory.

Both targets compile the production `AudioOutput.cc`. The Harmony target exercises first-frame deferral, mute changes, announcement order and text modifications, queue overflow, initialization once, failure/retry, object destruction, headless initialization, and worker-thread announcements. The synchronous target checks the existing non-Harmony initialization, mute, speech conversion, and overflow behavior.

Qt signals, queued delivery, object lifetimes, and the audio queue run normally. Deterministic test doubles provide the external speech engine, mute Fact, and application window lookup. Platform speech output and on-device launch timing require device verification.
