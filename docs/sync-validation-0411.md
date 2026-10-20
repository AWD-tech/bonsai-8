# Bonsai 8 0.4.11 — built candidate, hardware untested

The integrated 0.4.11 candidate is built and its software checks pass. It has
not been flashed or measured on the SP-1. The default website installer remains
the 0.4.6 test candidate.

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
These software results do not establish physical tempo stability or listening
quality; the original hardware reproduction must still pass.

## Device profiling and remaining gates

Thread runtime statistics are enabled for the existing read-only `DDPROF?`
response. Comparing counter deltas distinguishes audio, storage/planner and
other thread activity. Storage-thread totals also include refill and host-command
work; they are not a direct measurement of planner-only CPU time.

The existing elapsed planner-slice counters include preemption. In particular,
subtracting audio preemption still leaves control, USB and other interrupt work.
Use thread statistics alongside those counters to investigate the remaining
deadline problem; do not convert host benchmark savings into device CPU claims.

After integration checks and application-only flashing, verify runtime identity
and library preservation, then repeat the unchanged 0.4.10 workload with all
eight stems. Changed tempo on both decks, the 0.5x–1.25x range, USB audio with
mirroring, audible quality and recording/save/export remain hardware gates.
Successful host tests alone do not authorize promotion to the default installer.
