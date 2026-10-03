# Physical test — 2026-09-30

Initial 0.1 binary SHA256: `e5144826ee19f260b62296695ef9a055d45788f71847f14462783c2c71dc80fc`.
Previous 0.2 binary SHA256: `9b9c182c43f7cc2912b9fc7bd2d50f74de0211f371047f0f40f2fd1243178304`.

## Version 0.3 — flashed; first runtime results below

The user confirmed audible playback on 0.2, then requested full physical mirroring and live device audio in the browser. The previous website inferred button depression from play/mute state, drove lamps from browser audio meters, and had no animated side status LEDs. Firmware 0.2 did not transmit physical button/fader/LED state. A regression through the actual app callback/render loop reproduced a stuck PLAY key; it passes with the new telemetry path.

Candidate 0.3 transmits timestamped physical controls and the eight PWM LED duties using read-only `DDMIR?` requests. A 64-frame queue, bounded 16-frame replies, overflow count and stale-state handling prevent silent loss claims or indefinitely stuck buttons. No storage commands are used while monitoring. ADC fader readings remain distinct from deck gains during pickup; the mixer is unchanged.

Candidate 0.3 also changes the unused host-to-device UAC2 endpoint into a stereo USB capture endpoint. It streams the actual post-master mixer buffer at 48 kHz, with a bounded SPSC ring and 47/48/49-frame asynchronous packets. Audio copy/send failures cannot block device playback and have separate counters. Device sample positions track both deck timelines, including pause and cue. These are implementation and software-test results, **not yet USB enumeration, audible capture, latency or sustained hardware results**.

C ASan/UBSan suites cover the eight-voice mixer, telemetry edges/overflow/wrap and stereo capture ordering/underflow/overflow/wrap. Ten Python transfer tests pass. The website has 21 passing JS tests including the real application mirror path, fragmented serial negotiation, short taps, stale/lost updates, input-device filtering and device-based progress. Firmware builds within flash/RAM limits. Flashing completed; runtime physical testing remains pending power-on. No songs were rewritten for these changes.

At 23:09 UTC the connected bootloader accepted all 23 application pages (398 chunks, 90,392 bytes) and both CRC-valid finalize replies. The application-start command was sent. Binary SHA256: `bd49980db111657d92e3a5ffc7155b44dff255ccf5dd1aaa2afd1f2d78b73aa6`. No eMMC commands were sent. Evidence: `hardware-tests/flash-20260930-190903.jsonl`. This establishes bootloader acknowledgement, not binary readback. The runtime did not enumerate in the following 20 seconds; the user was asked to hold FUNCTION 1.5 seconds to power on. USB audio and post-update library verification remain pending that physical step.

The published website's Model tools were opened and exercised in the actual browser: part selection changes view, outward and overall height sliders update their readouts, settings survive reload, and no browser errors were reported. Evidence: `../virtual-sp1/tests/model-tools-live.jpg`. The user adjusted values during this check; those settings were retained.

## Initial 0.1 observations

- SP-1 bootloader responded with CRC-valid status packets, USB `2367:1701`.
- Uploaded the 87,756-byte application at `0x20000` in 386 chunks across 22 pages, using the published Solderless updater sequence. Erase and finalization responses had valid CRCs. No eMMC initialization/upload commands were sent.
- The first host script incorrectly assumed the first H response must equal the number of chunks. The device returned zero. The published updater instead passes that returned value into a second H command. Continuing that documented sequence returned a valid I response; the application was then started. The acknowledgement is not a flash readback check.
- After reconnecting and powering on, the device enumerated as **SP-1 Dual Deck**, USB `2fe3:5210`. Zephyr 4.3's sample initializer hardcodes `0x2fe3` despite the configured VID. Host discovery now accepts the observed descriptor and requires the exact product string.
- Diagnostics identified `sp1-dual-deck-0.1`. Eleven consecutive samples over ten seconds showed zero underruns, read errors, bad blocks, unsupported blocks, clipping, I2S errors and eMMC CRC errors. The fault marker was `4294967295` (no recorded fault).
- Maximum measured idle audio block time was 1,780 microseconds, below the 5,333-microsecond deadline. This is an **idle** measurement, not an eight-stem performance result.
- `storage=0`: normal storage writes remain locked. This value alone does not distinguish an unsupported library format from an eMMC initialization failure. Existing songs have not been verified or backed up.
- Host diagnostics now skip startup/blank lines and reject missing/incomplete status. Seven Python tests plus the shared C mixer sanitizer suite pass.
- A further 51-sample control test observed A/B deck changes, PLAY state changes on both decks, changes from all four faders, and master-volume changes. No faults or I/O errors appeared. The user reported no visible/audible response; the incompatible library explains the absent playback. Remaining buttons, LED feedback and audio response are not yet verified.
- Read and rechecked the existing first 8,192 storage bytes without writing. The saved index SHA256 is `15110a3db4e834a4d42a910c5af1cd8ffa566e26bcfe827c251d27f1edfb2f9f`. Its magic is S816 (`0x53383136`), the older Tape Looper raw-PCM format. Four slots contain four stems each. This confirms the card can be read and the format mismatch caused `storage=0`; the earlier storage uncertainty is resolved. This is an **index-only** backup, not a song-audio backup.

## Version 0.2 and authorized library replacement

- The user authorized replacing the device library. The empty P14S index was initialized once; all sixteen desktop stems were converted and hashed. Mac originals are untouched; some device audio has been overwritten. The old index backup is not an audio backup.
- Installed 0.2 at 19:05 UTC, 87,932 bytes at application address 0x20000. Valid bootloader CRC acknowledgements and finalization are recorded in `flash-20260930-150533.jsonl`. After the user powered on, runtime identified 0.2 with `storage=1` and `bulk_write=8`.
- The original unpaced B request failed: one sector passed, but 2/4/8 sectors returned E after the four-second receive timeout. The firmware's receive ring is 1,024 bytes and silently discards overflow. Two-millisecond host spacing still failed for eight sectors. Five-millisecond spacing passed short tests but failed during a longer upload. The host now splits every batch into single-sector B commands, waiting for each acknowledgement; this also avoids the driver's separate asynchronous multi-sector path. The later failure's exact cause is not isolated.
- Sixteen paced eight-sector trials passed, each independently read back after flush. A further 64-sector test through the corrected `Device.write_verified_blocks` passed with an exact SHA256 match. Fixture throughput was 74,505 bytes/s. Evidence is in the three `bulk-*-validation-20260930.json` reports and `bulk-validation-20260930.json`.
- No underruns, read errors, bad blocks, unsupported blocks, clipping, I2S errors or storage CRC errors were reported. The no-fault marker remained 4294967295. Maximum audio callback time during these idle/transfer tests reached 5,298 microseconds; this does not establish eight-voice playback performance.
- Added regression tests modelling the bounded receive ring and stopping on the first failed acknowledgement. The ring test failed with the old sender; single-sector commands satisfy the ring bound without timed pacing.
- The corrected single-sector sender completed 6,704 verified writes over 35 seconds, with repeated flushes and an independent 64-sector post-flush readback matching the fixture SHA256. Measured rate was 98,065 bytes/s. Diagnostic error counters stayed zero. All ten Python tests and the C mixer ASan/UBSan suite pass; this is transfer evidence, not a playback test.
- The library worker resumed at 19:33 UTC from 11.6%, after rechecking the saved checkpoint endpoints, then stopped safely on a multi-sector error after 20 seconds. Slot 1 bass reached a durable checkpoint of 5,120 sectors; no full song was published. Four-song completion and final checks were still pending at that point; see the completion section below. See `single-verified-validation-20260930.json` for the subsequent host workaround validation.

## Upload recovery history

At 19:48:47 UTC, slot 1 completed and its metadata/x3 table passed exact readback. At 19:51:55 the host hit a serial write timeout during slot 2 vocals. The device still responded with zero diagnostic errors and no reset. Read-only recovery checks matched slot 1 metadata/x3 and first/middle/last sectors of all five saved stem checkpoints. Host load was extremely high; scheduling delay is suspected, not established. The worker resumed from 10,240 slot 2 vocal sectors with a 30-second bounded write deadline. No library initialization was repeated. See `resume-preflight-20260930-1954.json`.

The Mac entered forced low-power sleep at 20:03:12 UTC with 1% battery and woke at 20:19:37 UTC. This invalidated the uploader's USB handle. The device re-enumerated with unchanged diagnostics, no reset and zero error counters. Recovery verified the published metadata/x3 plus three sectors from each of six saved stem checkpoints, then resumed slot 2 drums at 14,336 sectors while the Mac was charging. Evidence: `resume-preflight-20260930-2020.json`. This was a host suspend interruption; uninterrupted long-duration playback remains untested.

Slot 2 was published at 20:28:23 UTC. At 20:34:42, single-sector B rejected block 883354 in slot 3 bass. Three post-flush reads returned identical incorrect contents (496 mismatched bytes). An identical-payload rewrite passed, followed by exact post-flush readback of all 668 sectors from 882687 through 883354. The exposed diagnostic counters stayed zero despite this failed transfer; they do not cover every upload error. The underlying cause is not established. Preflight also matched both published songs' metadata/x3 and three sectors from all 11 checkpointed stems. Upload resumed at 20:37 UTC from the preserved checkpoint, without publishing partial song 3. See `sector-failure-20260930-2035.json`, `sector-repair-20260930-2036.json`, and `resume-preflight-20260930-2037.json`.

Slot 3 was published at 20:40:17 UTC. At 20:47:00 a second single-sector B write was rejected at block 1157539 (slot 4 drums). Readback differed in 505 bytes. One identical-payload rewrite succeeded, and all 421 sectors from 1157119 through 1157539 matched after flush. Preflight verified the three published songs and three sectors in all 14 checkpointed stems, then resumed at 20:49 UTC. No library initialization was repeated. Evidence: `resume-preflight-20260930-2048.json`. The cause of these isolated failed writes is unresolved.

## Four-song completion and final verification

All four songs were published by **21:00:37 UTC on 2026-09-30**. The worker exited and released USB. Saved checkpoints contain all sixteen full stem lengths, totalling 253,474,816 bytes. Slots 1–4 contain Forgiveness, Ego (Live in London), the Meek Mill/Busta Rhymes mashup, and Gimme Dat Ting.

At 21:04 UTC, a separate verification pass rehashed every prepared file, matched the saved manifest fingerprint, checked the live 0.2 firmware and storage layout, and flushed before reading. The device index and checksum-valid extended metadata matched every manifest record. Slots 5–16 remained empty. First, quarter, middle, three-quarter and last sectors of all sixteen stems matched their source bytes; both previously repaired sectors (883354 and 1157539) also matched: **82 post-flush sectors total**. Every audio sector was verified during writing; the final independent pass sampled the stored audio rather than rereading the entire library.

After exiting transfer mode, `storage=1`, error counters and underruns remained zero, the reset value stayed 4, and the no-fault marker was 4294967295. Both decks were paused, with slots 1 and 2 selected. The observed 5,298-microsecond maximum audio callback is an idle/transfer measurement, not an eight-voice result. USB was closed and is available to the website. Diagnostic counters did not detect the earlier rejected writes, so they are not standalone proof of storage reliability.

Evidence: `hardware-tests/library-verification-20260930.json`, `final-library-index-20260930.bin`, `final-library-x3-20260930.bin`, and the upload JSONL. The verification script is `hardware-tests/verify_completed_library.py`; it does not initialize or rewrite audio. Mac source files were untouched.

## Remaining physical tests

The user confirmed audible playback on 0.2. Version 0.3 audible playback, two-song/eight-stem mixing, remaining controls/LED feedback, headphones/speaker output, sustained playback timing, normal power-off, power-cycle persistence, and USB-unplug endurance remain unverified. Bootloader entry and the 0.3 update were exercised. The website has 21 passing host tests; live physical mirroring, USB audio capture and playhead synchronization still require runtime verification.

Raw local evidence is in `hardware-tests/`: flash/finalization JSONL, startup enumeration, raw status and idle status JSONL. These logs are not a binary readback or an audio recording.

## First 0.3 runtime check and 0.3.1 candidate

At the next powered-on connection, USB enumerated as SP-1 Dual Deck (2fe3:5210), with bcdDevice 0x0300. The live website negotiated physical mirroring and showed Deck A Song 1 change from paused to playing; its device-derived position advanced from 00:02 to 01:01 of 02:37. This establishes runtime telemetry and advancing playback time, not complete physical LED/button correspondence or audible USB audio. The user has been asked to compare faders, FUNCTION, PLAY and a track button against the model.

macOS did not expose an SP-1 audio input in CoreAudio. AppleUSBAudioControlNub attached to interface 2, but no audio engine appeared. A read-only GET_DESCRIPTOR captured the 193-byte configuration in hardware-tests/usb-0.3-config.bin. Clock CUR returned 48,000 Hz and RANGE returned one fixed 48,000 Hz range. The website's Listen here reported no named SP-1 input and stopped the unidentified default microphone without connecting it to speakers/analyser. Browser CDC ownership was respected; no competing serial handle was opened.

Candidate 0.3.1 uses an internal fixed clock with read-only frequency, musical-instrument category and embedded synthesizer input terminal. USB bcdDevice is 0x0301. It built successfully at 90,392 bytes, RAM 194,932 bytes; SHA256 71c7cd930b25382ddf466a1702e28215c11950a095bd5ba9f86c94915b40cc7c. Descriptor compatibility remains a hypothesis until a runtime test. The 23:35:54 UTC flash attempt waited 55 seconds but no bootloader or other USB device enumerated; no firmware/storage writes occurred. The user was asked to reconnect while holding Track 1 + Track 4.

The website now preserves audio connection failures across status polls and prevents a second Listen here request while permission is pending. The regression failed before the fix; all 35 JS tests pass afterward. Published permanent version 01M3TASQ8H8TYGSC9SFZ6F746V loaded with cache version 20260930-audio-status-07 and no browser warnings/errors. Live failure-state persistence will be checked when hardware is connected again. Approved geometry remains unchanged and developer tools remain absent.

## 0.3.1 installed and enumerating

At 23:48 UTC the bootloader accepted 90,392 application bytes (23 pages, 398 chunks) with both CRC-valid finalize replies; application start followed. SHA256: 71c7cd930b25382ddf466a1702e28215c11950a095bd5ba9f86c94915b40cc7c. No library write commands were used. Evidence: hardware-tests/flash-20260930-194840.jsonl. Acknowledgements are not binary flash readback.

After the user powered on, runtime identified sp1-dual-deck-0.3.1. macOS SPAudioDataType listed SP_1 Dual Deck, manufacturer softmodded, two USB input channels at 48,000 Hz. This resolves the previously observed enumeration failure on this Mac. Audible capture and sustained stream performance remain separate tests.

The independent post-update verifier matched the manifest, index, extended metadata, all four slots and 82 representative post-flush audio sectors including both previously repaired sectors. Slots 5-16 remained empty; diagnostics stayed clear and USB was released. Evidence: hardware-tests/post-0.3.1-library-verification.json. No song audio was rewritten during this verification. The user also reports being able to change songs.

## 0.3.1 playback and capture follow-up

The website subsequently showed both deck timers advancing and live changes of deck, mutes and pitch. It later lost USB; after reconnection one browser handshake timed out. A separate guarded status request succeeded, so this was not established as a firmware crash. The user confirms physical playback and controls still work.

Diagnostics later recorded deck underruns of [1769, 1680], then [2043, 1939], with read/block/CRC/I2S errors and clipping still zero. These are real starvation events in the shared mixer and require investigation; clean idle checks do not establish two-deck reliability. Maximum observed audio callback was 4,464 microseconds.

A six-second native capture selected only the named SP_1 Dual Deck input. USB reported 6,212 packets with zero capture underflow/overflow/send errors, but the saved PCM was entirely zero (253,696 stereo frames). The status immediately afterward showed A playing with all four stems muted and gains zero, while B was paused. This is not evidence of successful audible capture or proof that USB caused the silence. Browser monitoring remains unverified. Evidence: hardware-tests/audio-runtime-0.3.1.json and the local capture log. The audio recording and raw device logs remain outside Git.

The later browser reconnect succeeded. Listen here changed to Stop listening, the site reported Live stereo mix from SP-1 at 48 kHz, and a non-flat waveform was visible while Deck B Song 3 played with all four gains at 100%. This confirms the browser received non-silent device audio and connected its monitoring graph; it is not a listening-quality or endurance result. Both deck timing displays remained present, and browser warnings/errors were absent. Screenshot: ../virtual-sp1/tests/live-usb-audio-0.3.1.jpg.


## Version 0.3.2 — dropout investigation, 2026-10-01 UTC

The user reported choppy mixing on both the physical output and the website. The website was replaying the physical USB mix. With Song 2 instrumentals on A and Song 4 vocals on B, both at 1x, two eight-second read-only probes recorded [103, 118] and [131, 134] new deck underruns. The latter ran with browser monitoring and USB capture stopped. Storage read errors, bad blocks, CRC errors, clipping and I2S errors stayed zero. With B paused, a further eight-second measurement recorded [0, 0] new underruns. This isolates the failure to the two-deck workload rather than website mixing or capture alone. It does not identify a single subroutine as the sole cause.

Candidate 0.3.2 reduces the mixer's CPU load: full-range interpolation uses a bounded unsigned 32-bit product instead of 64-bit arithmetic, preserving truncation toward zero, and voices whose gain ramps have reached zero skip interpolation while their buffers/playheads remain synchronized. The C renderer passes exhaustive fractional-phase comparisons against a 64-bit reference for full-scale, sign-changing and representative sample pairs. Existing ASan/UBSan mixer, capture and telemetry suites plus ten Python tests pass.

The 90,392-byte application (194,932 bytes RAM) was flashed at 00:16:30–37 UTC. SHA256: `07ff1930938b9d1d225017adf4c425ac92d3fb25681fe5e72c0d1be573f6e930`. The updater acknowledged both finalizations with valid CRCs and started the application. No song-library commands were sent. Runtime identified 0.3.2. Playback still produced underruns while the controls were moving; maximum audio callback time was 3,411 microseconds. The CPU optimization alone was insufficient.

Reproduce with `.build-env/python/bin/python tools/check_playback.py --seconds 8 --output hardware-tests/dropouts.json`, after starting both decks and releasing the website's serial connection. Use `--playing a` for the single-deck control. The check fails on new underruns or storage/output faults and rejects changed playback settings. Raw audio and hardware traces remain local and are not part of the repository.


## Streaming reader and USB load — 0.3.3 / 0.3.4

0.3.3 connected the existing upstream chained-DMA reader to the Dual Deck refill path, reserved its required two-byte CRC tail, and checked every sector's CRC (no rate-based sampling). Runtime and flashing were confirmed. A stable 20.022-second two-deck run at 1x passed with zero new underruns or read/block/CRC/I2S errors. The user confirmed smooth physical playback. Both songs retained all four source stems; four of the eight gains were nonzero. This was Songs 1 and 2, not the original Songs 2 and 4.

USB capture exposed a remaining failure: during a stable 20.038-second recording, each deck accumulated 314 underruns, with capture underflow/overflow/send and storage error counters still zero. A separate eight-second mirror-polling test without capture produced [23, 22] underruns. These measured load cases prevent calling 0.3.3 a complete fix.

Candidate 0.3.4 waits for room for eight sectors before refilling instead of topping off individual sectors. That reduces command and scheduling overhead. The refill threshold leaves at least 2,112 native frames buffered before starting the refill (70.4 ms at maximum 1.25x speed for P16M; more for P14S). Full CRC checks, bounded retries, synchronous fallback, control/audio priorities, library format and write path are preserved. `refill_blocks`, `refill_ms`, `refill_retries` and `refill_fallbacks` expose reader behavior. The test command supports `--mirror` to reproduce the website's 30 ms mirror / 500 ms status cadence while a named-device USB capture runs independently.


## 0.3.4 / 0.3.5 follow-up and Bonsai 8 0.4.0 candidate

The user subsequently reproduced choppiness with all four stems audible on each deck. Muting two stems on each deck had sounded smooth. A 0.3.4 baseline without USB capture recorded zero new underruns over eight seconds, but a later USB/mirror run recorded [449,446] over thirty seconds while controls changed. The changing gains prevent a controlled comparison; the observed starvation remains a failure.

0.3.5 is the last firmware confirmed installed, at 00:33:13-20 UTC on October 1. Application SHA256: `97d9e954f6e6773e5581246d8b6353ceaf7a6f9ba2a0b55aa5fc3efc52409aa5`. It adds optional thread timing diagnostics. An audible five-second run used about 48% CPU in audio and 28% in storage, with eighteen additional underruns on each deck. Gains changed during the measurement, and it was not a controlled all-eight-stems pass. Earlier diagnostic runs labelled eight-stem were not eight audible voices. These labels must not be treated as evidence of a passing maximum-load test.

Bonsai 8 0.4.0 is a new build candidate, not an installed or hardware-verified release. Its stable-gain renderer avoids repeated per-sample state checks and optimizes normal-rate interpolation. Desktop differential tests compare exact output and state against the reference across pitch, ramps, starvation, ring wrap and effects. Desktop timings improved; they do not establish nRF52840 CPU deadlines. The effects path is substantially more expensive than dry mixing and requires a maximum-load test before being described as reliable.

The candidate adds read-only library listing, independent song loading into either deck, offline post-master mix recording into an empty slot, and the effects selected during the user interview. The website adds library management and application-only firmware installation. Recording is buffered and fails closed on overflow or write failure. Saving waits for both transports to pause, flushes storage, compares every recorded sector through an accumulated checksum, verifies metadata writes, and publishes the new slot last. The inherited metadata format has no journal; power interruption during a metadata sector write is not claimed to be atomic. Existing audio slots are not overwritten. No recording, deletion, upload, or new flash was performed on the device during this October 2 software work.

The user elected to test hardware later. Required checks remain: full eight-stem playback dry, with USB audio/mirroring, all selected effects, and offline recording; non-unity speeds; browsing/loading one deck while the other continues; recording stop, pause/save, power cycle and site export; safe delete/upload interruption; browser flashing and runtime identity. A successful build and sanitizer suite do not close the dropout issue or establish audibility, control ergonomics, or sustained storage throughput.

Software verification for the October 2 candidate: six C suites pass under ASan/UBSan (engine, mirror, USB capture, recorder, effects, metadata), plus eleven Python transfer/codec tests. The companion website passes 52 Node tests and local UI checks using explicitly synthetic slots. The final application is 104,848 bytes; static RAM including reserved stacks is 245,716 of 262,144 bytes. SHA256: `81381d9ed34a52f357275f6f70e39c939f7eb69005de8bf9a0db414eb3960bae`. This hash identifies the unflashed 0.4.0 candidate. Effect clipping is reported separately as `fx_clips`; lowering master volume does not undo clipping already produced inside a stem effect.


## Bonsai 8 0.4.0 flash and startup — October 2, 2026

At the user's request, the 104,848-byte candidate was flashed on October 2 at 19:37 EDT (23:37 UTC). SHA256: `81381d9ed34a52f357275f6f70e39c939f7eb69005de8bf9a0db414eb3960bae`. The bootloader passed three fresh-state checks; 26 application pages were sent and both finalization replies passed CRC checks. The application start command followed. This updater does not support application readback. No song-library commands or writes were sent during flashing.

After the user powered on, read-only status at 19:39 EDT confirmed `firmware=bonsai-8-0.4.0`, `storage=1`, both decks paused, and zero read, block, CRC, I2S and FX clipping errors. Normal-runtime `DDLIB?` listed slots 1–4, each with four stems, at 7,583,452; 7,658,128; 7,592,368; and 11,820,304 source frames at 48 kHz. This confirms the library listing, not a fresh audio-sector checksum or audible playback test. The host serial handle was closed after inspection.

Local evidence: `hardware-tests/flash-20261002-193734.jsonl` and `hardware-tests/bonsai-0.4.0-postflash.json`. The earlier unflashed status above is historical. Full eight-stem performance, USB/mirroring under load, effects, recording and browser flashing remain unverified; do not promote the candidate to a hardware-verified release based on this flash and startup check.

## 0.4.0 playback follow-up — October 2, 2026

The user reports stuttering with all eight stems. Initial dry samples recorded zero new underruns, but changes in stored gains made both runs inconclusive. One thirty-second sample began with all eight gains at 256 and ended with four at zero; it does not establish sustained eight-stem performance.

After the website released its serial connection, a stable 10.044-second measurement reproduced the failure: **124 new Deck A underruns and 133 new Deck B underruns**. Read, bad-block, CRC, I2S and USB capture error deltas were zero. USB capture was inactive throughout, with no packets sent. The fault therefore persists without browser monitoring.

The measured configuration was slots 3 and 2, both playing at 1x, no mutes, master 192, A gains `[256,256,256,256]`, and B gains `[0,0,0,0]`. Filters on A stems 1 and 2 had amount 2; the other six effects were bypassed. These are four audible stems with two streaming decks, despite the intended eight-stem setup. The session maximum audio callback was 5,718 microseconds, above the 5,333-microsecond block deadline; this maximum is cumulative and does not by itself isolate the cause.

The next controlled comparison is the same settings with those two filters bypassed. Effects, storage scheduling and song-specific behavior remain hypotheses; no new fix or flash is claimed. Evidence: `hardware-tests/bonsai-0.4.0-stutter-site-disconnected.json`. The read-only test released serial after completion. Full-load dry playback, USB capture/mirroring, effects and recording remain open hardware gates.
