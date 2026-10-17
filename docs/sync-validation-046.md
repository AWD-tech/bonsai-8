# Bonsai8 0.4.6 live-sync host validation — 2026-10-17

**Result: functional checks pass; audio screening fails. Keep 0.4.6 a candidate.**
The unchanged production DSP reproduces seven tone/stereo screening failures and
one transient-alignment failure. No device, serial connection, firmware rebuild,
flash, or publication was used. The already distributed binary was not altered.

## Reproduce

From the repository root:

```sh
sh tests/run_live_sync.sh                 # exits 1: functional + failing audio screens
sh tests/run_live_sync.sh --functional    # exits 0: controller and bounded streaming
sh tests/run_live_sync.sh --quality       # exits 1: pulse and tone/stereo screens
```

The script compiles the actual `dual_engine.c`, `bonsai_sync.c`, and
`bonsai_stretch.c` with Apple Clang 21, C11, `-O1 -Wall -Wextra -Werror`, AddressSanitizer,
and UndefinedBehaviorSanitizer. It uses no additional Python packages. Full measured
output is in [sync-validation-046.txt](sync-validation-046.txt).

Tested firmware source: `e480559816a65db67ba4a0915281165657126f15`, without executable
source changes. SHA-256:

| Source | SHA-256 |
| --- | --- |
| `bonsai_sync.c` | `25931ff3a4d03211ff656763cb6bd1e76f42b3c5be81196b2fdad1220a44752a` |
| `bonsai_stretch.c` | `74eb2741c1f5ae6c4bdd990f6d1021ff564c34d873491824d7e482df46e2fbbf` |
| `dual_engine.c` | `7cc9a977a881d0ff889dae850ec2933ea4071dd015bdf6805824f7e92f8c1355` |

## Passing evidence

| Check | Observed result |
| --- | --- |
| Existing `tools/test.sh` baseline | Nine C executables and 50 Python tests passed before concurrent validation changes; no sanitizer failures. |
| BPM/tempo range matrix | 245 combinations across 20–300 BPM and master rates 0.5–1.25x; 91 exact accepted ratios, 154 refused without clamping success. |
| Actual engine taps and following | Four taps teach 120/150 source BPM, then B follows A through the production worker/render path; first nominal lock after 5.477 s. |
| Master change and nudge | Master changes to 0.9375x at 30 s; follower nudged at 45 s; final follower 0.75x and final/max-last-5s error 0.015417 beats, inside the 1/64-beat nominal lock tolerance. |
| Sustained bounded streaming | 600 simulated seconds, eight populated stems, sixty paired tempo changes, mute/unmute, 4096-frame source rings and eight-sector refill batches; zero underruns/clips, source clocks never seek backward. |
| Bass/mid tone checks | All 30, 40, 55, 80, 110, 220, and 440 Hz antiphase tone cases pass across seven rates. |
| Stereo integrity | Exact antiphase preserved in 56 tone cases; exact zero in the silent channel for left-only/right-only signals; all 55/220 Hz stereo cases pass. |

The soak charged 1,406,904,578 worker operations; its busiest callbacks used all
64 available worker batches. This is a deliberately generous host schedule,
**not an MCU deadline or storage-bandwidth measurement**. Both decks' snapshots
share the same output clock. The controller observes actual applied tempo and
nominal source clocks, as the firmware adapter does.

## Reproducible audio failures

Tone setup: four identical unity-gain stems on one deck, 10,000-sample peak sine,
native 24 kHz, production stereo 48 kHz renderer. After one second warm-up, measure
131,072 output frames with a Hann-window FFT (0.366 Hz bins and interpolated peak).
The synthetic screening limits are: dominant peak within 1% of input frequency,
RMS between 70% and 110% of input, and at least 90% spectral energy within
`max(2 Hz, 2% of input frequency)`. These declared engineering screens are not
substitutes for a listening test or established hardware acceptance criteria.

| Input | Requested rate | Measured failure |
| --- | --- | --- |
| 20 Hz antiphase | 0.75x | Only 88.45% energy near input frequency. |
| 20 Hz antiphase | 0.97x | Dominant peak 19.399 Hz, about −3.0%. |
| 20 Hz antiphase | 1.03x | Dominant peak 20.605 Hz, about +3.0%. |
| 20 Hz antiphase | 1.125x | Dominant peak 22.051 Hz, about +10.3%; only 68.11% energy near input frequency. |
| 20 Hz antiphase | 1.25x | Dominant peak 20.451 Hz, about +2.3%. |
| Stereo: 55 Hz left, 263 Hz right | 0.75x | Left peak 54.419 Hz, about −1.06%; right 263.115 Hz. |
| Stereo: 55 Hz left, 263 Hz right | 1.25x | Left peak 55.571 Hz, about +1.04%; right 262.881 Hz. |

The dominant spectral peak describes this synthetic signal; it does not establish
the perceived pitch or quality of arbitrary music. All failures cause exit 1.

Transient setup: A has a 2 ms rectangular pulse every 12,000 native frames
(120 BPM) routed left; B has one every 9,600 frames (150 BPM) routed right.
Both grids start at frame zero; B follows A at 0.8x. After ten seconds settling,
the next thirty seconds contain **71 B onsets instead of 60**, including eleven
inter-onset gaps below 50 ms. Mean/max nearest-beat errors are **9.026/14.521 ms**.
The controller's nominal error stays below **0.000292 beats (~0.146 ms)**.
Onsets are rising crossings above 1,000 output sample units; the pulse peak is
5,000. This fails both the onset-count check and the audible 1/64-beat screen
(7.8125 ms at 120 BPM), even while nominal sync remains locked.

## Narrow fix directions; not implemented

- **Sub-bass:** inspect bounded candidate search and grain phase selection in
  `bonsai_stretch.c`. Its ±480-frame search spans 40 ms total, shorter than a
  20 Hz cycle (50 ms). An adaptive/longer low-frequency search is a focused
  hypothesis to test, with new worker-budget and ring-retention measurements;
  it is not yet a validated fix.
- **Independent stereo tones:** the shared stereo candidate selection can favor
  one channel's phase. Investigate channel-normalized correlation or candidate
  phase error while preserving one shared grain for all stems/channels. Do not
  independently shift channels as a shortcut. Rerun the exact 55/263 Hz fixtures.
- **Extra transients:** inspect the sparse-reference/natural-continuation rule
  and overlap handling for onsets just outside the old tail. Constrain candidate
  selection so one source onset does not reappear in adjacent grains, and retain
  this pulse test. Nominal-clock servo changes alone would not demonstrate a fix.

These failures localize to production DSP behavior, but the proposed causes and
remedies still require investigation. No firmware source was changed to hide or
work around them.

## Still unverified

Physical control gestures, MCU timing/preemption, real eMMC refill behavior,
recording/USB workloads during sync, analog output, and musical listening across
real tracks remain unverified by this run. Synthetic host passing results cannot
clear those release gates; the observed audio failures independently prevent a
clean pitch-preserving live-sync acceptance claim.
