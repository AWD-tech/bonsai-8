# Bonsai 8 0.4.12 hardware candidate

0.4.12 is **not a release**. It applies the actual-song planner optimization
documented in `sync-validation-0411.md`: early rejection of impossible isolated
onsets and bounded source/analysis/tail batches, with no persistent RAM increase.
Local sanitizer, exact-output and audio-quality checks passed before building.

## Pinned artifact and preservation

Source: `4d95761f7ab8ccd88fa7353e5634c53ee4e51af6` (`source_dirty=false`).
Application: 122,012 bytes; RAM: 254,756 / 262,144 bytes.
SHA-256: `3dbad90a5dd48a567aa1aef97c09bdfd95ee67aced460b2c537828e9d96f56e9`.

Application-only flashing completed October 20 at 02:56 EDT, with 30 pages,
536 chunks and both CRC-valid finalizations acknowledged. The updater observed
the expected USB disconnect about 117 ms after requesting application start.
This is not application-flash readback. Private log:
`hardware-tests/flash-20261020-025540.jsonl`.

Runtime reports `bonsai-8-0.4.12`. All five song entries match the last verified
0.4.11 listing; eight metadata sectors and 51 representative audio sectors match
the previous hashes. The player was already in bootloader when work began, so
there was no fresh preflash runtime listing. This is not a full audio readback.
Evidence: `hardware-tests/bonsai-0.4.12-postflash.json`.

## Stable eight-stem unity comparisons

All stable runs used slots 1/2, master 8, both decks playing at 1x, all eight
gains fixed at 256, no mutes and unchanged controls. No monitored storage, I2S,
capture or clipping errors increased.

| Workload | Seconds | New source underruns A/B | New plan underruns A/B | Mean audio callback us |
| --- | ---: | --- | --- | ---: |
| USB audio off, no mirror | 20.045 | 0 / 0 | 0 / 0 | 1710.38 |
| USB audio off, 100 ms mirror | 20.108 | 0 / 0 | 0 / 0 | 1699.43 |
| USB audio on, no mirror | 20.061 | 292 / 303 | 0 / 0 | 2692.20 |
| USB audio on, 100 ms mirror | 20.114 | 287 / 296 | 0 / 0 | 2667.86 |

The initial 30 ms mirror run also had source underruns, but transport and gain
changes invalidate it as a stable comparison. It is retained as raw evidence.

The named USB input captured non-silent 48 kHz stereo. This demonstrates active
USB transport, not clean audio or browser speaker playback. The user reported
stuttering during capture and then that it stopped when the test ended.
Counter evidence isolates USB audio as a trigger: mirroring alone passes and
USB audio without mirroring still fails. No tempo planning was performed in
these unity runs (`planner_units=0`), so the planner optimization cannot explain
this particular source-buffer failure.

Private reports: `hardware-tests/bonsai-0.4.12-eight-{dry,mirror-only,usb-only,usb-unity-100ms}.json`.
Callback timings include interrupt preemption; starvation also selects the
slower fade/recovery path, so these numbers are not exclusive mixer CPU costs.

## Tempo changes with eight stems

The physical input settled with **both decks at 66191/65536 (~1.01x)**. Two early
prechecks expecting only B to change were inconclusive and did not measure a
steady window. The subsequent reports below require all eight gains at 256,
both decks playing, no mutes and unchanged controls throughout.

| Workload at ~1.01x on both decks | Seconds | New missing-plan underruns A/B | Native source frames advanced A/B | Mean audio callback us |
| --- | ---: | --- | --- | ---: |
| USB audio and mirroring off | 20.054 | 207 / 207 | 220,777 / 221,165 | 2428.10 |
| USB audio and 100 ms mirroring on | 20.099 | 156 / 155 | 166,382 / 165,575 | 2587.14 |

Both tests **fail**. Source-underrun counters and all monitored error/clipping
deltas stayed zero. Fewer counted missing plans under USB does not indicate
better playback: the decks advanced only about 6.9 seconds of source audio in
the USB run, versus about 9.2 seconds without USB, over approximately 20 seconds
of elapsed time. Miss counts mark starvation episodes, not their duration.
These are different workloads from the previous one-deck 1.02x test, so they
are not a controlled performance comparison against 0.4.11.

USB was active throughout the combined test: 20,081 capture packets and 176
mirroring replies. The recorded USB stream was non-silent 48 kHz stereo, but
the failed counters rule out a clean-playback claim. Evidence:
`hardware-tests/bonsai-0.4.12-eight-tempo-observed-dry.json` and
`hardware-tests/bonsai-0.4.12-eight-tempo-both-usb.json`.

The candidate's three CI jobs passed in run 39383418343. The previous redundant
run 39381583427 was cancelled after its browser job stalled; identical website
and workflow files passed in the newer run. Host/CI success does not override
these physical failures.

## Next isolation and release status

USB capture alone causes source starvation at unity; changed tempo causes plan
starvation even without capture. These must be addressed separately.
Compared with a successful same-song 0.4.7 USB run, capture copy/packet work has
not increased. Runtime thread accounting added in 0.4.11 is a small, falsifiable
configuration-only comparison for USB headroom, not an established cause.
Storage scheduling and the slower starvation/recovery renderer can amplify
initial shortages. No such new configuration or scheduling change has been
built or flashed in this test session.

USB capture has been stopped and the serial port released. No 0.4.12 release,
installer staging or site deployment occurred. The default installer is
unchanged. Physical tempo stability, clean USB playback, and the remaining
recording/browser release gates are still open.
