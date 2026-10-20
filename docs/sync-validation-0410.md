# Bonsai 8 0.4.10 tempo scheduling candidate

## Reproduced problem

The 0.4.9 hardware test held both decks and all eight stems steady for 20.032
seconds, with A at approximately 1.02x and B at 1x. A reported 87 new missing-plan
underruns and advanced only 93,708 native frames; B advanced 480,768. No source
underruns, storage errors, clipping or control changes occurred. The user heard
the resulting stutter. This is a failed tempo test, not a verified release.

## Change

The previous planner waited for the first 16 milliseconds of an audio grain to
finish before beginning any search. Private search work can safely start earlier;
only the copy into the shared overlap tail must wait. A completed early search
parks its small descriptor per deck and releases the shared scratch buffer, so a
paused deck cannot monopolize the worker. The renderer still sees a plan only
after its full tail is copied and release-published. Exclusive reset invalidates
parked and partial work.

The next build also reduces repeated private-stage checks and window calculations.
The matching window, candidate search, scoring and pitch/quality thresholds remain
unchanged. These changes do not alter song storage or physical control mappings.

The status response distinguishes decks still using the stretcher from untouched
1x playback. `plan_audio_us` counts audio-callback preemption during planner slices.
Subtracting it from `plan_us` still includes controls, USB and other interrupt work;
it is not a measurement of exclusive planner CPU time.

## Required verification

The bounded host deadline reproduction, phase/pause/reset regressions and exact
PCM comparisons must pass alongside the existing sanitizer and audio-quality
screens. Host scheduling budgets do not model the SP-1 processor. Even earlier
planning may be insufficient if total CPU or source-admission time is too high.

After the build is flashed, repeat the same stable physical workload and inspect
new missing-plan/source counters, planner timing and source-clock progress. Then
test both changed tempos, USB capture/mirroring and listening. Runtime and song
preservation also require fresh checks. No new physical pass is claimed here.

## Software validation and built artifact

Nine C sanitizer suites, 59 Python tests, the 114 synthetic audio-quality screens
and 600 simulated seconds with eight stems and 60 paired tempo changes pass.
The bounded deadline regression fails with the old admission gate and passes with
early planning; 22 deck phase offsets and pausing/resuming either deck pass.
Separate old/new executables retained all 56 tested grain descriptors and PCM
hashes across seven ratios. The existing 5,400 renderer comparisons also pass.

CACHE and REFERENCE work now validate once per bounded private batch. Shared TAIL
writes keep their per-frame validation. The exact existing quadratic window is
constant-folded into flash; candidate scoring order and window values are unchanged.

The application uses 116,948 bytes of flash and 255,564 / 262,144 bytes of RAM.
Binary SHA-256: `85f76ea087b71a9b825a6f06d647c5ec6943d9023a8e1e9f874bae177801fda1`.
It is a hardware-test candidate; no 0.4.10 physical pass is claimed.
