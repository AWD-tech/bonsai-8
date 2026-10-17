# Evidence required to promote 0.4.6

The release tool previously validated only the application flash, preserved song
listing and steady playback windows. It could output `status: verified` even
when the recording, live-sync and browser checks were still open. Those steady
checks remain useful, but they do not certify those features.

For 0.4.6 onward, `tools/release.py --feature-evidence FILE` also requires the
following physical-device results for the exact binary:

| Check | Required coverage |
| --- | --- |
| `playlists` | `save`, `restart`, `browse_a`, `browse_b` |
| `recording` | `sustained_capture`, `save`, `restart`, `full_export`, `audio_quality` |
| `live_sync` | `manual_tempo`, `beat_follow`, `phase_nudge`, `pitch_preservation`, `bass_and_stereo`, `audible_quality` |
| `browser_audio` | `named_usb_input`, `non_silent_stereo`, `monitor_output`, `disconnect_reconnect` |
| `browser_flash` | `application_only`, `runtime_version`, `library_preservation` |

The JSON report contains `firmware` (for example `bonsai-8-0.4.6`),
`binary_sha256`, and a `checks` object keyed by the names above. Every check must
contain `verdict: "PASS"`, `environment: "physical-device"`, its `coverage`
array, and a nonempty `evidence` array. Each evidence entry has a `path` to a
local artifact and that file's hexadecimal `sha256`. Relative paths resolve
beside the report. Keep raw traces and music under the ignored `hardware-tests/`
directory, not in Git or the public site.

Evidence must state what actually happened, including duration, workload,
versions, faults, comparisons and limits. A host simulation uses the
environment `host` and cannot pass this gate. A browser running a synthetic
MediaStream is a host test even when the browser and Web Audio graph are real.
A serial adapter running the site's parser is not a browser speaker-output or
browser-flashing test. A short successful take is not sustained recording.

The tool checks report completeness, binary identity, artifact existence and
artifact hashes before writing any release files. It publishes only coverage
and artifact counts, not private file paths or raw evidence. It cannot prove
that a human-authored report is truthful or that a recording sounds correct;
the reviewer must inspect the underlying artifacts. Never manufacture a
physical PASS from software fixtures to satisfy this schema.

The published 0.4.6 candidate retains its existing immutable binary. New host
coverage does not turn it into a verified release. If a test identifies a
firmware change, assign a new firmware version, flash it and repeat the relevant
physical checks before promoting that new binary.
