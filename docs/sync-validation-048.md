# Bonsai 8 0.4.8 tempo-performance candidate

On October 20, the 0.4.7 player passed steady eight-stem playback and USB capture,
but the user heard stuttering or pitch changes after a roughly 2% tempo increase.
New underruns and a cumulative callback peak above the 5,333 microsecond block
budget were observed. The transports had changed before the snapshot, so that
sample is not a controlled per-mode timing comparison.

## Changes

- Keep a settled dry deck on its efficient renderer while the other deck stretches.
  Preserve the dedicated fully dry loop for ordinary playback.
- Fetch all four stems' current/next stereo pairs once per deck frame. Reuse the
  previous next frame instead of reading it again after every native-frame advance.
- Use exact integer paths for whole and midpoint samples. Other phases retain
  the existing interpolation arithmetic and rounding.
- Publish the renderer-owned cursor with a release store, and read applied tempo
  once per callback, refreshing it at grain activation. Retention publication and
  source-buffer reclamation keep their original behavior.
- Report separate cumulative plan/source starvation counts. These identify the
  immediate blocked state; a missing plan can itself follow slow source reads.

No quality thresholds, control mappings, storage layout or application-only
flashing boundaries changed. Existing songs are not rewritten by this update.

## Host validation

The independent uncached 64-bit renderer oracle checks 5,400 exact PCM and state
comparisons: one or both stretched decks, dry partner, half-frame activation,
overlap and grain boundaries, ramps/mutes/master levels, missing plans/source
and uint32 wrap. A counted-source-read regression fails on 0.4.7 and passes with
the cache reuse. Nine C sanitizer programs and 59 Python tests pass. The unchanged
live-sync suite covers 245 controller cases, 114 rendered-audio quality screens
and 600 simulated seconds with eight stems and sixty paired tempo changes.

```sh
sh tools/test.sh
sh tests/run_live_sync.sh
sh tools/build.sh
.build-env/python/bin/python tools/package.py
```

These tests establish software equivalence and signal regressions, not the
physical player's deadlines. Host callback benchmarks are not MCU measurements.
Exact binary size, hash and RAM usage are recorded in the packaged manifest.

## Required physical checks

Confirm the runtime and preserved song index after application-only flashing.
Measure stable eight-full-stem windows at unity, with one deck at changed tempo,
and with both decks at changed tempo. Repeat with USB capture and website
mirroring. Check new counters as well as original underruns, storage/USB errors,
callback timing and listening for pitch, bass and stereo quality. Then exercise
beat following and phase nudges. Recording and browser release gates remain
separate; none is closed by this renderer optimization.
