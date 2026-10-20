# Bonsai 8 0.4.9 planner-performance candidate

The 0.4.8 physical test failed at roughly 1.02x on both decks: 61/62 new
underruns in a stable twenty-second window. Every underrun was classified as
waiting for a tempo plan; source-starvation and storage errors stayed zero.
The user heard stuttering or pitch changes. The renderer optimization alone
did not fix the deadline problem.

## Change

The correlation loop previously checked four acquire atomics for every stereo
comparison. On the Cortex-M4 those checks include memory barriers. This build
batches scratch-only comparisons within one candidate and the caller's existing
work budget. Validation still precedes the next batch, tail writes and plan
publication. Accumulation order, scoring, search range, source reads and work
charges stay unchanged. No additional planner memory is allocated.

The exclusive-reset and single-worker contracts remain required. A stale work
item is rejected before it can update shared tail data or publish a descriptor.
The change does not alter control mappings, songs, recording format or the
application-only updater bounds.

The adapter now reports cumulative `plan_us`, `plan_calls`, `plan_units` and
`plan_max` for each service slice. Times include preemption by audio and USB;
they are elapsed wall time, not exclusive CPU usage. They must not be added to
audio callback time to estimate total CPU load.

## Validation and limits

An instrumented 43-second eight-stem host simulation retained the same charged
work, service calls and PCM checksum while validation calls dropped by about
83%. Host planner elapsed time fell in that comparison; neither figure proves
the SP-1 meets its physical deadline. Existing audio-quality thresholds remain
unchanged. Bounded-budget, odd-budget, stale-generation, renderer equivalence
and quality checks accompany this build.

```sh
sh tools/test.sh
sh tests/run_live_sync.sh
sh tools/build.sh
.build-env/python/bin/python tools/package.py
```

The linked application uses 114,888 bytes of flash and 255,540 / 262,144 bytes
of RAM. The packaged manifest pins its exact binary hash and source commit.
Hardware verification is pending: repeat eight-stem unity and changed-tempo
windows, then USB capture/mirroring and listening. Recording, beat following,
phase nudges and browser release gates remain separate. Do not label the
candidate verified until the corresponding physical checks pass.

## First device attempt

The first transfer was acknowledged but normal startup failed. Recovery to the
previous 0.4.8 binary succeeded, with the song preservation checks unchanged.
The planner change is inactive during unity-tempo startup, and no source-level
startup fault has been established. A difference in how long the updater keeps
the restart connection open is being tested separately with identical bytes.
See [the hardware report](../HARDWARE_TEST.md); this binary remains withheld.

## Retry and listening result

A retry of the identical binary kept the start connection open for the reference
updater's response/disconnect wait. It powered on and reported 0.4.9; all five
song entries, eight metadata sectors and 51 representative audio sectors matched.
This does not prove why the first attempt failed, since the entire image was
transferred again.

The user again reported stuttering after a tempo change. The following snapshot
recorded 343 cumulative missing-plan underruns on B, zero source underruns and
zero storage/I2S errors. By the snapshot A was paused and both requested speeds
were back to 1x, so it is not a controlled changed-tempo measurement. A subsequent
50-second observation retained that state with no additional underruns. The
listening failure still prevents promotion; the next investigation targets the
planner's delayed admission and use of its shared scratch buffer.
