# Two-deck browser verification — 3 October 2026

The site now has a local Web Audio instrument with two independent four-stem
songs. This is a browser mixer, not a compiled MCU/bootloader emulator. The
physical connected monitor still uses the player’s telemetry and USB mix.

## Implemented and checked

- A/B buffer loading, transport, seek/cue, gain, mute/solo, deck level, tape
  speed and native audio-clock loops use a shared output AudioContext.
- Short stems are padded with silence to the longest stem’s duration. They do
  not loop early or require animation-frame source restarts.
- Loading/cueing/pausing one deck leaves the other deck’s source nodes and clock
  intact. Selected-deck controls preserve the other deck’s settings.
- A session library can load any retained song into either deck. Removing a
  session entry is explicit; unloading one deck preserves the other.
- The combined bus has fixed 1/8 headroom. The native compressor’s threshold is
  0 dB to avoid its automatic makeup boost below the threshold. Ordinary bounded
  audio therefore does not receive dynamic compressor pumping.
- There is a finite 768 MiB decoded-buffer budget. It counts unique original,
  padded, prepared and retained-session AudioBuffers and checks padding/worker
  reservations. Over-budget loads preserve both existing deck transports and
  fail with instructions to remove unused session songs or use shorter audio.
  This is a decoded-audio budget, not a guarantee covering all model/worker/browser
  memory. Native decoding and separation still allocate temporary data.
- Local sources pause on physical connection. Incoming physical settings do not
  replace the browser songs’ saved gains, mutes, speed or buffers. Disconnecting
  restores browser UI without automatically starting its audio.
- A/B keys select a deck, Space controls it, Shift+Space controls both, and 1–4
  mute the selected stems. Native input/button/select/dialog keyboard behavior
  takes priority over those shortcuts.
- Prepared pitch/tempo buffers have a separate install hook preserving original
  source buffers, per-stem pitch metadata and analysis; prepared playback returns
  that deck’s tape speed to 1. PlaybackRate only implements coupled tape speed,
  never independent timing-preserving pitch.

## Actual browser evidence supplied by root agent

The root agent ran the real page through CUA on the local portless preview:
`http://bonsai-browser-mixer.localhost:8080`.

- Both generated demos played; selecting B, muting vocals and pausing B left A
  playing and unmuted.
- A 390 px viewport had no horizontal overflow. Deck cards stacked and their
  controls remained reachable. Screenshot: `two-deck-mobile-20261003.jpg`.
- The generated-signal OfflineAudioContext fixture rendered actual stereo PCM:
  independent bus/mute/volume result L/R 0.249996454 (expected 0.25), eight
  correlated full-scale stems peak 0.999986 with no clipping, and short-stem
  silence/native loops passed without UI ticks. Screenshot:
  `two-deck-audio-proof-20261003.jpg`.
- The original fixture caught a real compressor makeup issue: output was
  0.266952425 instead of 0.25. After the 0 dB threshold correction, all three
  actual Web Audio tests passed. This evidence is not a listening-quality claim.

The source fixture is retained in `browser-audio-offline.html`. To reproduce,
copy it temporarily to `public/__browser-audio-test.html`, open that preview URL,
and click its test button. Remove the public fixture before publication.

## Remaining limits

Node tests use audio/DOM boundaries for state and graph assertions; they do not
establish listening quality or device reliability. Root’s browser PCM checks
cover generated signals, not every song, browser, phone or sample rate. Large
file decoding, long mobile sessions, arbitrary audible loop boundaries and
worst-case model/worker memory remain unverified. Physical serial/audio paths
were not exercised by this implementation agent. Real beat/key analysis and
pitch quality have separate tests and audition requirements; no automatic live
physical stem pitch feature is implied.

## Layout revision

The 3 October layout revision replaces the tall workspace sidebar with a compact
model/loading row, full-width A/B cards, then the selected deck's stem controls.
Matching stays collapsed; player library and selected-deck export share the next
row. The duplicate selected-deck transport is hidden; visible play/cue/seek
controls live in the A/B cards. Mobile uses one vertical flow. Approved 3D
geometry and the audio engine are unchanged. The new loading destination selector
uses the same selected deck and disables during preparation or physical monitoring.

Automated suite after the revision: 122 passed, 0 failed. HTML has balanced tags
and 80 unique IDs. Root verified this revised layout at desktop width and 390 px with no horizontal
overflow. Current proof: workspace-layout-desktop-20261003.jpg and
workspace-layout-mobile-20261003.jpg. The earlier viewport screenshot above
predates this layout change.

The alignment follow-up applies one SVG chevron to all select controls, centered
vertically with a 13 px edge inset and 38 px reserved label space. USB connection
icons and status dots use centered grid/flex alignment; compact connection labels
update without deleting their SVG. Focused audio/mirroring tests passed after the
change. Root is performing the visual alignment check before publication.
