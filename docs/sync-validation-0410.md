# Bonsai 8 0.4.10 — physical tempo test failed

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

The build also reduces repeated private-stage checks and window calculations.
The matching window, candidate search, scoring and pitch/quality thresholds remain
unchanged. These changes do not alter song storage or physical control mappings.

The status response distinguishes decks still using the stretcher from untouched
1x playback. `plan_audio_us` counts audio-callback preemption during planner slices.
Subtracting it from `plan_us` still includes controls, USB and other interrupt work;
it is not a measurement of exclusive planner CPU time.

## Verification limits

The bounded host deadline reproduction, phase/pause/reset regressions and exact
PCM comparisons must pass alongside the existing sanitizer and audio-quality
screens. Host scheduling budgets do not model the SP-1 processor. Even earlier
planning may be insufficient if total CPU or source-admission time is too high.

The application was flashed and normal runtime/library preservation passed.
The stable physical workload below still failed. Both-deck changed tempo,
USB capture/mirroring and listening remain open gates; host results do not close
them. The default installer remains 0.4.6.

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
It is a failed tempo-test candidate and is withheld from the default installer.


## Physical result — October 20, 2026

The application-only update acknowledged 29 pages/514 chunks and both CRC-valid
finalizations. Normal runtime reports `bonsai-8-0.4.10`; five song entries,
eight metadata sectors and 51 representative audio sectors remain unchanged.
These checks do not establish application-flash or full audio readback.

A **20.018-second** run kept A at 1x and B at 66846/65536 (~1.02x), both playing,
all eight gains at 256, master 8, no mutes and no USB capture. Controls did not
change (`changed: []`). B reported **96 new missing-plan underruns**, A zero;
source underruns and all monitored error/clipping deltas were zero. A advanced
480,384 native frames while B advanced only 103,402. Mean audio callback time
was 2,764.87 microseconds. Earlier planning therefore did not fix the device's
tempo failure.

The planner completed 5,166,384 charged units in 5,256 calls. Elapsed slice time
was 5,536,639 microseconds, including 2,626,695 microseconds of audio preemption.
The 2,909,944-microsecond remainder also includes control/USB/other interrupts;
it is not exclusive planner CPU time. The counters identify the immediate
missing-plan condition, not a complete root-cause diagnosis.

Private evidence: `hardware-tests/bonsai-0.4.10-postflash.json` and
`hardware-tests/bonsai-0.4.10-observed-tempo.json`. See
[the hardware report](../HARDWARE_TEST.md). No artifact or installer promotion
follows from these failed measurements.
