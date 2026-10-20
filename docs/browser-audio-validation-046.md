# Browser audio validation for the 0.4.6 companion

Checked 2026-10-17 against the local `feat/bpm-playlists` companion, based on commit `e480559816a65db67ba4a0915281165657126f15`. The existing monitor implementation passed all nine native browser integration cases, including one minute of sampled stereo output. No product code or release status was changed.

This is **real browser Web Audio with synthetic input**, not a physical SP-1 or speaker listening test. Chrome ran headless in a new temporary profile with `--mute-audio`. Its native audio graph rendered and reached the native output destination; PCM was measured immediately after the monitor's gain. No sound was deliberately sent to the user's speakers. The firmware remains a candidate.

## Reproduce

From `site/`, using an available Playwright installation:

```sh
PLAYWRIGHT_MODULE=/Users/admin/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs \
PLAYWRIGHT_CHANNEL=chrome \
AUDIO_VALIDATION_REPORT=../docs/browser-audio-validation-046.json \
node tests/live-audio-browser.mjs

node --test tests/live-audio.test.js tests/mirror-app.test.js tests/browser-audio.test.js
```

Omit `PLAYWRIGHT_MODULE` when `playwright` resolves normally. Omit `PLAYWRIGHT_CHANNEL` to use Playwright's installed Chromium. This machine had Playwright 1.62.1 but lacked its matching Chromium revision, so the recorded run used installed Chrome 155.0.8059.80 with Node v22.22.3. No browser download was required.

The runner intercepts every page request and serves only explicitly listed workspace files at the synthetic origin `https://bonsai-audio-validation.test`. That hostname is an isolated test fixture, not a running or published website. Unexpected requests fail the run. No server or port is opened, and no existing browser tab, serial port, browser permission, microphone, or chooser is used.

To include the saved-recording export check, generate a synthetic fixture first, from `site/`, then run the browser command above in the same shell:

```sh
export RECORDING_FIXTURE_DIR="$(mktemp -d)"
PYTHONPATH=../tests python3 - <<'PY'
import os
import subprocess
from test_record_adapter import RecordingAdapterTests

RecordingAdapterTests.setUpClass()
try:
    subprocess.run([str(RecordingAdapterTests.exe), "fixture",
                    os.environ["RECORDING_FIXTURE_DIR"]], check=True)
finally:
    RecordingAdapterTests.tearDownClass()
PY
```

This uses the host C compiler and the recording adapter test's synthetic storage; it does not contact hardware. Without `RECORDING_FIXTURE_DIR`, the report explicitly marks this additional export check as not run. The standalone browser runner takes about one minute because it includes sustained monitor sampling.

## What is real and what is simulated

`tests/live-audio-browser.fixture.js` imports the actual, unmodified `public/live-audio.js`. Audio generation, stereo channel merger, `MediaStream`, tracks, source node, analyser, gain, destination, waveform canvas, clock progression, context closure, and track stopping are native Chrome objects. Thin constructor and connection instrumentation forwards directly to native Web Audio and records graph connections and cleanup.

The fixture generates distinct 997 Hz and 1499 Hz tones at amplitudes 0.4 and 0.2, routes them into a native stereo `MediaStreamAudioDestinationNode`, and supplies that stream to the production monitor. `enumerateDevices()` and `getUserMedia()` are replaced with an in-memory adapter. Only the generated track's name and device ID are overridden to exercise named-device validation. Browser or OS capture selection is therefore not proven by this test.

For mute, the generated input becomes silent and then resumes. The monitor has no public mute method; this case verifies incoming silence and recovery. Separately, the existing offline mixer harness uses the actual `AudioEngine.mute()` path. Disconnect is simulated by stopping a native track and dispatching an `ended` event; this tests the production disconnect callback, not physical USB removal.

## Results

The machine-readable evidence is [browser-audio-validation-046.json](browser-audio-validation-046.json). The tested `live-audio.js` SHA-256 is `1e962565088e742c0b3fb6d6cb09e93d585909c57beb06f5f8a438da7c3b6086`.

| Native browser check | Result |
| --- | --- |
| Stereo signal through source → production analyser → gain → native destination | Pass, 48 kHz. Post-gain RMS L 0.212222 / R 0.106092; expected 0.212132 / 0.106066. |
| Production waveform canvas and analyser | Pass, nonzero waveform pixels and input RMS 0.159137. |
| Incoming mute and unmute | Pass, both measured output channels reached RMS 0, then recovered. |
| Stop, including repeated stop | Pass, track ended, source/analyser/gain disconnected, context closed, references cleared. |
| 12 repeated starts while pending plus 12 while active | Pass, exactly one capture request and one production graph. |
| One-minute monitor with stereo RMS, frequency, and audio-clock checks | Pass, 60.139 seconds and 238 sampled windows. Left frequency 996.999894–997.000077 Hz, right 1498.999088–1499.000708 Hz. |
| Cancelled capture resolves after a new session starts | Pass, stale track ended with `AbortError`; new graph retained measurable stereo output. |
| Stale and current disconnect events | Pass, stale event ignored; current event stopped the graph and called `onEnded` exactly once. |
| Hidden names followed by a named input | Pass, unidentified probe stopped; only named retry entered a graph. |
| No named device, wrong exact device identity, or named selection returning an unidentified input | Pass, all captures stopped and zero rejected inputs entered a graph. |

All 11 monitor contexts closed and all 14 synthetic tracks ended. No page errors or unexpected requests occurred.

The sustained check sampled 2,048 frames per channel approximately every 250 ms. RMS stayed within 0.212036–0.212228 on the left and 0.106031–0.106101 on the right. The audio clock advanced 60.138667 seconds over 60.138600 wall-clock seconds; the largest per-sample clock difference was 5.2 ms. These are periodic measurements, not continuous dropout detection or physical USB endurance evidence.

The runner also executed the existing `tests/browser-audio-offline.html` in Chrome. All three native `OfflineAudioContext` cases passed: independent deck buses and actual per-stem mute (PCM 0.249996454 versus 0.25 expected), eight correlated full-scale stems (peak 0.999986), and short-stem padding plus native looping without UI ticks.

The 50 targeted Node tests in `live-audio.test.js`, `mirror-app.test.js`, and `browser-audio.test.js` passed. These complement the native browser signal checks with mocked application state and lifecycle coverage; they are not additional physical tests.

## Host recording through browser export and native WAV decoding

The additional browser case consumes the recording adapter's actual host C output: the eight-stem engine feeds the recording, save, metadata, and reload paths using synthetic storage. See [recording-validation-046.md](recording-validation-046.md) for that harness and its separate evidence. No physical player contributed these sectors.

Chrome ran the actual `DeviceLibrary.export(12)` through an in-memory transfer adapter, then its native `OfflineAudioContext.decodeAudioData()` decoded the exported WAV at 24 kHz. The short take occupied 12 P14S sectors and contained exactly 1,664 stereo frames (69.333 ms). All exported WAV PCM bytes matched the native recorder's expected PCM. The export read 17 storage sectors, wrote none, and exited transfer mode.

Native WAV decoding preserved both channels, all 1,664 frames, the 24 kHz sample rate, and duration. The maximum floating-point difference from integer PCM divided by 32,768 was 0.00000292808, below the allowed one PCM16 step (0.0000305176). The original float tolerance of 0.0000001 rejected this small decoder conversion difference; byte-exact WAV verification already passed. The final harness reports both the actual difference and the PCM-step tolerance instead of claiming bit-exact browser float decoding. Decoded RMS was 0.033052 / 0.029853.

The JSON report includes SHA-256 hashes and sizes for all four synthetic fixture files. This validates the saved-data export and browser decoding path, not sustained physical recording, a real serial transfer, the website's download button, or audibility.

## Remaining limits

This run does not validate physical USB descriptors or capture, OS device naming/routing, browser permission/chooser behavior, actual speaker or headphone audibility, end-to-end latency, physical unplug events, behavior beyond the one-minute synthetic session, or other browsers. The standalone monitor harness does not load the full companion UI or establish an actual serial connection. No new hardware validation is claimed.
