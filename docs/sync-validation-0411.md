# Bonsai 8 0.4.11 — physical tempo test failed

The integrated 0.4.11 candidate is built and its software checks pass. It has
been flashed with CRC-valid application-write finalizations. Runtime identity,
five-song index, eight metadata sectors and 51 sampled audio sectors pass.
The twenty-second eight-stem unity baseline passed without new underruns or
errors. A stable A-at-1x/B-at-~1.02x test then failed with 131 new missing-plan
underruns on B. The default website installer remains the 0.4.6 test candidate.

## Failure this candidate addresses

The stable 0.4.10 test kept A at 1x and B at 66846/65536 (~1.02x), both playing,
with all eight gains at 256 and controls untouched. In 20.018 seconds B reported
96 new missing-plan underruns; source underruns and monitored storage, clipping
and other error deltas stayed zero. A advanced 480,384 native frames while B
advanced only 103,402. Earlier planning did not fix the device's tempo stutter.

See [the 0.4.10 report](sync-validation-0410.md) and
[hardware results](../HARDWARE_TEST.md). The private measurement is
`hardware-tests/bonsai-0.4.10-observed-tempo.json`.

## Renderer changes

A settled stretched deck can render a validated span using local copies of its
cursor, descriptor, gains and interpolation phase. This path requires published
source data, settled gains and envelope, and a span that ends before the final
midpoint or descriptor activation. Ramps, starvation, boundaries and other
ineligible cases retain the general renderer.

The span preserves sample rounding and the cached stereo pairs, including muted
and missing stems. It publishes the final cursor and clock once per callback.
Ring retention is also calculated once at the callback boundary. After a source
or plan check fails, the same callback does not repeat that check for each output
sample; the next callback checks again. The storage worker cannot publish new
data while the higher-priority audio callback is running.

## Planner changes

Correlation packs the weighted, channel-balanced reference into signed 16-bit
stereo pairs, using a common adaptive scale. Cortex-M4 uses a packed stereo
multiply-accumulate into a 64-bit sum; the host fallback uses equivalent signed
products. Candidate energy uses exact 64-bit rolling moments instead of
recomputing the weighted square sum at every candidate.

The rolling energy sums and packed integer products are exact for their defined
inputs. Quantizing the reference and changing floating-point accumulation order
changes score rounding and can change candidate selection near a tie. This is
therefore a quality-screened algorithm change, not a claim of bit-identical
planner output.

The 1440-frame sequence, 1056-frame hop, 384-frame overlap, ±600-frame search,
4096-frame source rings and quality thresholds remain unchanged. Private work
stays budgeted; shared tails remain protected by generation/sequence checks and
release publication. Song storage and physical control mappings are unchanged.

## Host evidence so far

- The renderer passed 11,352 exact PCM/state comparisons against the independent
  sample renderer, including overlap and grain boundaries, ramps, pauses,
  starvation, missing stems and index wrap.
- A host renderer benchmark's median fell from 11.704 to 4.128 microseconds,
  a 64.7% reduction. This measures that host workload, not SP-1 callback time
  or device CPU headroom.
- The private packed-planner variant passed the existing sanitizer-enabled
  live-sync quality and functional screen without changing thresholds.
  A separate rolling-energy oracle checked 47,860 stride-1/stride-8 transitions.
- Additional private regressions cover packed coefficient error, full-scale
  and random inputs, index wrap, poisoned scratch and budget partitioning.

The integrated source passes nine C sanitizer suites, 59 Python tests, all 114
audio screens and 600 simulated seconds with eight stems and 60 paired tempo
changes. Tracked kernel regressions include 1,625 exact candidate-energy checks
and 28 full-worker coarse/refine budget partitions. No quality thresholds were
relaxed. Logs: `/tmp/bonsai-0411-tests.log` and `/tmp/bonsai-0411-sync.log`.

The ARM build is 120,892 application bytes and uses 254,756 / 262,144 RAM bytes,
including thread profiling. Binary SHA-256:
`8b22168c1ae802cba09a1a1b516cfbae401a299c9590b9178cc9386a7bf7da8b`.
The immutable artifact comes from source `a6c60ef`; all three GitHub CI jobs
passed in run 39377562628. These software results did not resolve the physical
tempo failure below.

## Device profiling and remaining gates

The first stable device baseline ran for 20.071 seconds with both decks at unity,
all eight gains fixed at 256 and USB capture off. No controls changed and there
were no new plan/source underruns, monitored errors or clips. Both decks advanced
481,664 source frames. Mean callback time was 1,693.02 microseconds.
Scheduler-attributed shares were audio 31.5%, storage 29.0%, main 8.4% and idle
28.1%. Evidence: `hardware-tests/bonsai-0.4.11-eight-dry.json`.
This confirms the unity baseline only, not the tempo-performance correction.

A subsequent **20.034-second** test kept both decks playing, A at 1x, B at
66846/65536 (~1.02x), all eight gains at 256, no mutes and USB capture off.
Controls did not change (`changed: []`). B added **131 missing-plan underruns**,
while A added none. Both source-underrun counters and all monitored
error/clipping deltas stayed zero. A advanced 481,024 native frames; B advanced
only 140,057. Mean callback time was 2,241.32 microseconds.

The planner completed 7,252,320 charged units in 7,358 calls. Elapsed slice time
was 5,735,912 microseconds, including 2,110,042 microseconds of audio preemption.
The remaining 3,625,870 microseconds include control, USB and other interrupts;
they are not exclusive planner CPU time. Scheduler-attributed shares were audio
41.4%, storage 34.6%, main 8.2% and idle 12.7%, including interrupt attribution.
Evidence: `hardware-tests/bonsai-0.4.11-eight-tempo-b.json`.

Thread runtime statistics are enabled for the existing read-only `DDPROF?`
response. Comparing counter deltas distinguishes audio, storage/planner and
other thread activity. Storage-thread totals also include refill and host-command
work, and scheduler totals include interrupt attribution; they are not a direct
measurement of planner-only CPU time.

The existing elapsed planner-slice counters include preemption. In particular,
subtracting audio preemption still leaves control, USB and other interrupt work.
Use thread statistics alongside those counters to investigate the remaining
deadline problem; do not convert host benchmark savings into device CPU claims.

## Actual-song capacity reproduction

`tests/replay_tempo_budget.c` runs the production decoder, engine and planner
against eight caller-supplied P14S files. It never accesses the device, exports
audio or bundles private songs. Its input is a stated work capacity, not host
wall-clock speed. Source refills are free, so this is an optimistic service model,
not an MCU emulator or a substitute for hardware testing.

The same local song pair at 1x/~1.02x needs 1,257,210 charged units per second
with unrestricted service. Limiting the planner to 362,616 units per second
(the observed 0.4.11 rate, **not an established maximum capacity**) reproduces
129 B plan misses and 139,076 source frames advanced over 20 seconds, versus
131 and 140,057 on hardware. Capacity scenarios at 650,000 and 1,000,000 also
miss; 1,500,000 passes this workload. Work units are not equal-cost CPU cycles;
this comparison identifies a throughput gap but does not prove a future
optimization's MCU speedup.

The previous functional simulation allowed up to 64 worker slices per audio
callback without charging elapsed device time. Its zero-dropout result could
therefore coexist with this hardware failure.

A separate actual-song stage replay found that 466 of 500 B grains entered
onset lookahead, and 401 then ran the transient scan before falling back to
ordinary correlation. Those two stages accounted for about 34% of host planner
time and still validated shared state per sample. Unlike the earlier sine
benchmark, this reproduces a common expensive path in the user's music.
The private evidence is `hardware-tests/bonsai-0411-work-replay.json`; stage
results are under `/tmp/bonsai-stage-profile/`. Host stage shares are not device
cycle measurements.

The next correction targets this measured path and must preserve onset/pitch
quality. No new candidate or further flash is claimed here.
Changed tempo on both decks, the 0.5x–1.25x range, USB audio with
mirroring, audible quality and recording/save/export remain hardware gates.
0.4.11 is a confirmed failed tempo candidate and must not replace the default
installer.
