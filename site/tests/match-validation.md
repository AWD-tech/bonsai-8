# Browser matching validation — 2026-10-03

Implementation: `public/match.js`, `match.css`, `match-analysis.js`, `match-dsp.js`, `match-processor.js`, and `match-worker.js`. The matching component is wired into the browser decks; it is disabled while a physical player is connected.

## What is implemented

- BPM and beat phase from an onset envelope, normalized autocorrelation and weighted onset-time regression. Confidence is heuristic agreement, not a probability. Analysis uses at most the first 120 seconds, taking the drum stem when available. A more energetic stereo channel is used to avoid cancelling opposite-polarity stereo.
- Key from windowed FFT peaks, pitch-class accumulation and simple tonic/third/fifth-weighted major/minor profiles. Ambiguous/weak results are Unknown. This cannot guarantee harmonic compatibility.
- Editable source BPM, half/double correction, first-beat time and key; an explicit master deck; separate semitone offsets for the other deck's four stems. Same-mode key matching transposes vocals, bass and other; drums retain their manual offset.
- Real offline WSOLA/Lanczos processing, with independent pitch and tempo, in a terminable module worker. Original AudioBuffer data is copied before transfer. Output duration is calculated independently of pitch. Short stems pad to the shared song duration.
- Prepared buffers enter the same browser export and device-upload flow. A source identity guard prevents stale output from replacing a newly loaded song; action cancellation is checked across asynchronous context startup and before installation.
- Alignment schedules the follower's first beat against the master's next beat using the AudioContext sample clock. Loop-wrap scheduling is handled at cue time. Automatic continuing beat lock and quantized loop boundaries are not implemented.

## Automated evidence

`node --test tests/*.test.js`: **122 passed, 0 failed** at handoff. Matching contributes 24 tests covering:

- 24/44.1/48 kHz pitch shifts, exact output length, tempo changes retaining tone frequency, and combined changes.
- Distinct stereo frequencies, silent-channel isolation, unity bit accuracy, silence, invalid samples and memory/parameter bounds.
- Transient survival and placement within a 35 ms synthetic regression tolerance. This is a test bound, not a universal quality guarantee.
- BPM/phase recovery, a 120-second non-integer-BPM grid, major/minor test chords, silence/noise Unknown results and manual corrections.
- Actual module-worker imports and transferred stereo data, worker failure/cancellation, cancellation during AudioContext startup, duplicate-job rejection and original-array preservation.
- Prepared WAV export followed by P14S encoding, simulated verified device upload and device WAV export retaining the expected pitch/duration. The storage device and the identity-rate OfflineAudioContext boundary are mocked; no physical upload occurred in these tests.
- Prepared-session grid restoration, source-key correction, master selection and completion callback.
- Npm archive SHA-512 integrity, exact dependency version and distributed license/source availability.

## Real browser evidence reported by the root agent

Using the site's synthetic demos, Analyze both produced 120 / 119.99 BPM and Unknown keys. Preparing Deck B vocals at +7 semitones succeeded; the matched song appeared in the browser library and Align started both decks. Root then selected B and exported.

Although the computer-use tool's download-event wait timed out, root verified the completed local file `/Users/admin/Downloads/Upper circuit  demo B matched-stems.zip`: four stereo WAV files, each 44,100 Hz and 705,541 frames, with the prepared-stem README. This confirms the synthetic-demo browser export path. It does not establish quality for arbitrary songs, physical matched-file playback, long mobile jobs or live hardware pitch matching.

## Dependency and processing limits

Pinned `@soundtouchjs/core` and Lanczos dependency: 2.1.1, MPL-2.0. Both verified original npm archives, the corresponding TypeScript source and build metadata at upstream commit `d9e39a7ddcf74c7a145bbba45856f30068c82b79`, full license and modification notice are hosted under `public/vendor`. The sole vendor runtime edit replaces one bare npm import with a relative URL. See [upstream release/API](https://github.com/cutterbl/SoundTouchJS/tree/v2.1.1/packages/core), [license](https://github.com/cutterbl/SoundTouchJS/blob/v2.1.1/LICENSE), and [original SoundTouch parameter definitions](https://soundtouch.surina.net/).

Preparation accepts mono/stereo, 8–96 kHz, pitch within one octave, tempo 0.5–2, and source/output up to eight minutes, subject to a 384 MiB extra working-memory estimate and the audio engine's aggregate memory budget. Large jobs fail before rendering; there is no silent eviction. The processor changes waveform structure: no formant preservation or phase-locking across all four independent stems is promised. Transients can shift slightly. Fixed-tempo analysis cannot follow live tempo changes; the user must verify/correct grids and listen. These are prepared browser files for export/upload, not new automatic matching capabilities running inside the physical SP-1.
