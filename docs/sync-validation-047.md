# Bonsai 8 0.4.7 live-sync candidate — October 18, 2026

This build corrects the reproduced 0.4.6 bass, stereo and isolated-transient
defects. It is prepared for physical testing, not a hardware-verified release.
No SP-1 was accessed or flashed during this work. The published 0.4.6 binary and
its historical reports remain unchanged.

## What changed

- The source search covers a complete 20 Hz period: ±600 native frames instead
  of ±480. Candidate energy uses the same window as the reference correlation.
- Shared grain scoring balances channel frequency and reference power, so a
  faster or louder channel does not systematically pull the other off pitch.
  Power compensation is capped at 100x; silent channels contribute no score.
  All four stems and both channels still use one common grain/timeline.
- Isolated attacks are anchored to their nominal output time. The aligned tail
  prevents replaying a displaced copy of the same attack. A bounded incremental
  lookahead checks the whole upcoming grain without allocating another cache.
- Sustained backgrounds retain ordinary overlap. A transient must have observed
  quiet at both scan edges; clipped longer events cannot masquerade as short
  attacks and abruptly cut the output. Missing optional lookahead falls back to
  an ordinary plan instead of stalling the producer at its refill boundary.
- Coarse correlation samples every eighth frame before full-resolution local
  refinement. Starting a plan clears scalar state only, avoiding a hidden
  9,408-byte scratch reset on every grain. Sample arrays are overwritten before
  use, with poison/budget-invariance regressions.

Transient anchoring follows the general local-unity-rate preservation approach
described in [Driedger's time-scale-modification thesis, section 6.3](https://audiolabs-erlangen.de/content/05_fau/professor/00_mueller/01_group/2011_DriedgerJonathan_TSM_MasterThesis.pdf).
This bounded implementation deliberately handles isolated attacks; it does not
establish transparent stretching of arbitrary polyphonic music.

## Reproduce and measured results

```sh
sh tests/run_live_sync.sh
sh tools/test.sh
npm test --prefix site
sh tools/build.sh
```

The actual production engine, stretch worker and beat controller run under
AddressSanitizer/UndefinedBehaviorSanitizer. Thresholds from 0.4.6 remain intact:
≤1% tone pitch error, RMS 70–110% of input, ≥90% nearby spectral energy, and
audible pulse phase within 1/64 beat. The pulse count also explicitly rejects
duplicate gaps below 50 ms. Complete measurements: [sync-validation-047.txt](sync-validation-047.txt).

| Check | Result |
| --- | --- |
| Tone/stereo screens | 105 pass: 77 antiphase tone cases from 20–3,000 Hz and 28 independent-stereo cases, including swapped channels and a 4:1 level difference. Silent-channel isolation stays exact. |
| Original 20 Hz failure | 19.995 Hz at every tested ratio; the 0.4.6 1.125x case measured 22.051 Hz. |
| Original 120/150 BPM pulse case | 61 boundary-inclusive onsets within the unchanged 60±1 gate, zero duplicate gaps, maximum phase error 0.188 ms. Previously 71 onsets, eleven duplicate gaps and 14.521 ms. |
| Expanded transient cases | Seven cases across 0.5–1.25x, varied beat anchors, 1–192-frame widths and 90/120 BPM: no duplicate gaps; maximum audible error 0.979 ms. |
| Sustained-background guards | Both 10:1 pulse/tone cases pass; ordinary overlap is retained and the quiet 220 Hz tone is preserved. |
| Controller | 245 BPM/rate cases, four real-engine taps per grid, following, master-rate changes and phase nudges pass. |
| Endurance | 600 simulated seconds, eight stems, sixty paired tempo changes and ring wraps: zero underruns/clips, no backward source-clock seek. |
| Boundary/budget regressions | Poisoned scratch, work-slice invariance, slow/fast P16M refill limits, clipped attack rejection and complete edge-adjacent attacks pass, including uint32 wrap. |
| Other regressions | Nine C sanitizer suites, 59 Python tests and 158 website tests pass. |

The endurance fixture charges 1,220,743,010 worker operations and permits up to
64 worker batches per render callback. This is a generous host schedule, **not
an MCU CPU/deadline measurement**. The device must still sustain this work with
real storage, USB capture, controls, mirroring and recording.

## Build and remaining device gates

- Runtime version: `bonsai-8-0.4.7`; USB device revision: `0x0407`.
- ARM application: 114,328 bytes, linked at `0x20000`.
- RAM: 255,508 / 262,144 bytes; 6,636 bytes unallocated after reserved sections.
- Binary SHA-256: `bc263e96ff5d2aaab4b102355b093dba9402514647ff836e0f3d1992170a7437`.
- Exact source hashes and commit are supplied by the packaged manifest.

No controls, playlists, recording/storage layout or updater write regions changed.
Flashing remains application-only; actual library preservation on 0.4.7 must
still be measured. Recording's previously documented final metadata
acknowledgement ambiguity is unchanged.

Use the [return-to-device checklist](DEVICE_CHECKLIST_047.md) for flash/preservation,
real-song pitch and attack listening, eight-stem USB/mirroring/volume tests,
record/save/restart/export and longer endurance. A green host gate does not
authorize a hardware-verified label or replace those physical results.
