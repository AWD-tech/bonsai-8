# Bonsai 8 completion design

Read-only feasibility review, 3 October 2026. This is a proposed design, not a claim that these features exist. This review changed no firmware, website implementation, device storage, or installed application. It performed no build, flash, serial operation, deployment, or Git mutation.

## Recommendation

Complete the browser's two-deck engine and durable song names first. Analyze beat grids and keys in the browser, then render matched stem sets there for upload into empty device slots. These prepared songs can subsequently mix and record on the disconnected SP-1. Treat arbitrary live, independent, timing-preserving pitch shifts on all eight physical stems as a separate DSP feasibility gate; the current memory and measured playback failures do not support promising it.

For geometry, use the owner's photographs and original illustrated guide as visual references, then obtain a small set of caliper measurements. No factory SP-1 drawing or factory dimension table was located. A community development-enclosure STEP file exists, but is not evidence of the original enclosure's exact geometry.

| Priority | Deliverable | Practical result | Remaining limit |
| --- | --- | --- | --- |
| 0 | Establish reliable full-load playback | Eight dry stems, effects, USB, and recording measured independently and together | A build or desktop benchmark does not establish physical reliability |
| 1 | True two-deck browser engine using the shared C renderer | Two songs, eight independently controlled stems, matching effects/control behavior | Browser behavior emulation is not an MCU/bootloader emulator |
| 1 | Titles with content identity, then optional on-device metadata | Names survive reconnects and eventually move with the player | An old unnamed slot must not acquire a guessed title |
| 2 | Browser analysis, editable beat grid, key estimate, real stretching/pitch DSP | Tempo matching, phase alignment and independent pitch without changing timing | Analysis can be uncertain; changing a musical mode is not a global semitone shift |
| 2 | Upload pre-rendered matched stems to spare slots | Prepared matches work without a computer | Preparation is not arbitrary live per-stem pitch on the physical player |
| 3 | Measured geometric model | Dimensions tied to this physical SP-1 and an explicit tolerance | User measurements are not factory CAD or zero-error metrology |
| Research gate | One live timing-preserving physical pitch voice, then more | Possible limited device feature if measured resource limits permit it | Do not advertise eight independent processors without worst-case evidence |

## What the current code and evidence establish

Paths below are relative to this firmware repository unless prefixed `../virtual-sp1/`.

- `dist/manifest.json` identifies the 0.4.1 software candidate: 106,576 application bytes and 246,484 of 262,144 bytes RAM reserved. That leaves **15,660 bytes**, approximately 15.3 KiB, before adding another static allocation. Existing reserved stack space is already included; stack high-water headroom still requires measurement.
- `boards/teenageengineering/stem_player/stem_player.dts` targets the nRF52840. Nordic specifies a **64 MHz Cortex-M4 with FPU, 1 MB flash and 256 KB RAM**. The application's existing partition begins at `0x20000`; bootloader space is not spare application memory. [Nordic product specification](https://www.nordicsemi.com/Products/nRF52840).
- The device investigator's hardware documentation identifies a 4 GB eMMC on a **one-bit DAT0 interface**, not RAM and not an operating-system filesystem. The current firmware validates actual capacity through EXT_CSD. The chip's nominal capacity cannot replace that device check. [SP-1 hardware investigation](https://github.com/timknapen/SP-1-dev/wiki/Hardware-overview).
- `dual_engine.h` allocates eight rings of 4,096 stereo int16 frames: **131,072 bytes for the samples alone**. At native 24 kHz that is about 171 ms per voice at 1x, or 137 ms at 1.25x. `bonsai_record.h` adds an 8,192-frame recording ring: 32,768 sample bytes. `dual_capture.h` adds 8,192 sample bytes for USB capture. `bonsai_fx.h` holds eight 1,024-sample int16 wet histories plus state. These allocations have existing playback, recording and USB purposes; reclaiming them removes safety margin or functionality.
- The device output callback renders 256 frames at 48 kHz, so its deadline is **5.333 ms**. It has about 341,333 CPU cycles per block at 64 MHz for all application/interrupt activity, not a private budget for pitch DSP.
- `HARDWARE_TEST.md` and `hardware-tests/bonsai-0.4.0-eight-two-filters.json` record a controlled 0.4.0 failure with all eight gains at 256: **109/104 additional deck underruns in 10.045 seconds**, USB capture inactive, two filter amounts at 2. This does not isolate the filter as the cause. The cumulative 5,718 microsecond maximum callback exceeds one block deadline, but is not a per-test maximum. 0.4.1 host improvements do not establish a hardware pass.
- Eight P14S stereo sources at 24 kHz require about **0.702 MB/s** of payload reads at 1x and **0.878 MB/s** at 1.25x, before eMMC commands, CRC, retries, decode and scheduling. One P14S recording adds about 0.088 MB/s of writes. These figures are calculated from 140 frames per 512-byte sector; they are not measured sustained throughput. `sp1_emmc.c` uses the existing 32 MHz SPIM path in the 48 kHz build, with a documented out-of-spec clock tradeoff. Increasing that clock is not a justified fix.
- `dual_engine.c` has one speed and source read position per deck. Fractional resampling changes duration and pitch together. No beat detector, musical-key detector, or independent timing-preserving stem pitch processor is present.
- `../virtual-sp1/public/audio.js` currently provides **one** four-stem browser engine. `playbackRate` changes its speed and pitch together. Its source restart and main-thread loop handling are not a complete eight-stem emulation.
- `../virtual-sp1/public/live-audio.js` receives the device's **final stereo mix**. The current USB descriptor exposes stereo capture, not eight separate stems or a host-to-device audio return. Independent pitch processing cannot recover the original stems from that mixed input.
- Source names are not retained by the Bonsai/Tape Looper slot table. `dual_firmware.inc` emits `title:null`; library rows use slot numbers. The older factory `ALBUM_PRESENT` format did contain text fields, but it is a different format, not a compatible title extension to the current table. [Original-format investigation](https://github.com/timknapen/SP-1-dev/wiki/Album-metadata-format).

## 1. Automatic tempo, beat and key matching

### Define the three operations separately

**Tempo matching** changes beat spacing to a target BPM. **Beat matching** also aligns the correct beat phase/downbeat and continues following it; setting identical BPM or launching both transports in one callback is insufficient. **Key matching** transposes selected pitched material by a chosen interval while preserving its timing. Matching two tonic labels does not guarantee compatible harmony, phrasing, mode, or a good-sounding mix.

For constant tempo, if source B is at `bpmB` and master A at `bpmA`, B's source-consumption ratio is `bpmA / bpmB`, while its output-duration ratio is `bpmB / bpmA`. A real stretcher must preserve pitch during that tempo adjustment. Pitch ratio for `n` semitones is `2^(n/12)` with output duration unchanged. A variable-tempo song needs an editable piecewise beat map, not one average BPM.

### Site-assisted analysis and confirmation

1. Analyze original local music or aligned stems in a worker. Prefer rhythm-rich material for beats and pitched material for key; compare estimates rather than assuming a vocal-only stem always has a reliable beat grid.
2. Store beat timestamps in source frames, the downbeat/bar anchor, BPM estimate/segments, key tonic and mode, tuning offset, confidence/strength, analysis version, and user corrections. Keep one shared temporal map for every stem of a song.
3. Let the user audition a metronome and move the first-beat marker. Provide half/double tempo correction and an Unknown key. Do not label a low-confidence estimate as certain or silently transpose percussion.
4. Explicitly designate the master deck. Sync the other deck on a future output beat, with pre-roll for DSP latency. Keep start alignment, ongoing ratio and key matching independently controllable. Reversing the master is an explicit operation, not a side effect of selecting a deck for its faders.
5. Use a monotonic audio sample clock for event scheduling; UI animation timers only display the state. Preserve original source position, derived output position and beat position as separate fields.

Essentia is one candidate to evaluate: its rhythm extractor returns beat locations/BPM/confidence, and its key extractor returns key/scale/strength. The selected rhythm algorithm requires a 44.1 kHz analysis signal. These are estimates, not guarantees. Its published license conditions and model/dependency terms need review before distribution; a private repository does not make browser-delivered code private. A license-compatible analysis implementation or commercial license is an alternative. [Rhythm extractor](https://essentia.upf.edu/reference/std_RhythmExtractor2013.html), [key extractor](https://essentia.upf.edu/reference/std_KeyExtractor.html), [license information](https://essentia.upf.edu/licensing_information.html).

### Offline physical use

The first practical standalone version is a **prepared match**:

1. In the site, choose A as reference and a B song, confirm their beat grids, and select B's pitch interval for each desired stem.
2. Render B's four stems onto the shared target beat timeline, with independent pitch intervals but identical output timing. Preserve stereo coherence, shared leading silence and exact common lengths.
3. Audition against A, then upload B's derived four-stem song into an **empty** physical slot. Retain the originals and a provenance record linking derived audio to source content, grid, pitch settings and DSP version.
4. Add a device quantized-launch command that schedules a target start on the device sample clock using stored grid metadata. Preload the new deck before the boundary. Existing FUNCTION + PLAY is not this feature. A timestamped readiness reply must distinguish queued, ready and launched.
5. Mix, mute, add effects and record on the unplugged player using its ordinary renderer. For variable-tempo references, either bake B to A's full warp map or prepare both onto one constant-tempo grid.

This genuinely provides pitch-preserving preparation, but is not arbitrary pitch changes during performance. If the user changes one prepared deck's tape speed afterward, the prepared beat/key relationship can drift. Call the feature “Prepared match” until ongoing synchronization is actually implemented. Avoid pre-rendering every pitch/tempo combination; the physical library has only sixteen slots and each derivative consumes storage.

## 2. Independent per-stem pitch that preserves timing

### Preferred browser implementation

Use a real time-stretch/pitch library in WebAssembly, off the UI thread. Each stem keeps its own pitch parameter, while the four stems share a deck tempo map and output frame clock. Stereo channels of a stem must be processed together. Match algorithm delay across all eight paths, including bypassed paths, and compensate the initial padding before declaring a cue sample-aligned.

Rubber Band supports independent time and pitch ratios; its output demand is variable, so wrap it with bounded input/output rings and explicit latency handling. Its real-time output length and startup delay need compensation; offline rendering is easier to trim to a prescribed length. It is GPL or commercially licensed, so do not silently incorporate it into an incompatible distributed application. [Rubber Band](https://breakfastquay.com/rubberband/), [integration notes](https://breakfastquay.com/rubberband/integration.html).

SoundTouch is another candidate: its WSOLA-like time stretch and sample-rate transposer combine to produce pitch changes of constant duration. The author reports roughly 100 ms typical time-stretch latency; use that only as an algorithm characteristic, not a benchmark of this device. Evaluate artifacts on vocals, drums and stereo material before choosing it. [SoundTouch README](https://soundtouch.surina.net/README.html).

Do not implement “independent pitch” merely by setting eight `playbackRate` values. That makes stems consume different source positions and fall out of alignment.

### Physical feasibility gate

A modest illustrative 64 ms stereo float window at 24 kHz is 12,288 bytes for one voice, before output buffers, search/FFT state, overlap history and code. Eight such windows are 98,304 bytes. This is a sizing illustration, not a claimed allocation for either library. It shows why dropping a general desktop stretcher into the current 15,660-byte remainder is not justified.

If unrestricted live physical pitch remains mandatory, prototype **one** voice with an explicit bounded algorithm and range after dry playback is reliable. Measure static state, worst callback time, all thread high-water marks, storage underruns and audible artifacts while all other stems, USB and recording run. Then determine how many voices fit. A reduced-quality granular shifter, narrower range, lower processing rate, disabled recording, or fewer active processors is a product tradeoff requiring a clear user choice; none meets “eight unrestricted independent high-quality pitch voices” by renaming it.

Do not use eMMC as randomly addressed audio scratch or assume a larger flash capacity solves RAM pressure. Do not shrink playback rings to make room before characterizing worst storage stalls. Hardware replacement or a host-rendered mix is the honest fallback if the unrestricted requirement exceeds this MCU; the latter would additionally require a new USB playback endpoint and is not offline operation.

## 3. Complete eight-stem browser behavior

### Shared engine, distinct modes

Compile `dual_engine.c`, `bonsai_fx.c` and suitable recorder/codec logic into a versioned WASM module. Keep the same integer semantics for a **Bonsai compatibility mode**: native 24 kHz source representation, 48 kHz renderer, both deck transports, gains, mutes, speed ranges, headroom and effect character. Refactor gesture/pickup logic into a platform-independent controller so browser holds, FUNCTION combinations and LED rules do not become a second divergent implementation.

Run audio in an AudioWorklet; load/decode/analyze outside its callback. Use one audio scheduling domain for both decks. Preallocate buffers, queue timestamped control events and handle missing data predictably. Do not perform network, file I/O, unbounded allocations, waits or DOM work from rendering. Do not hard-code the browser callback to 256 frames: adapt its delivered quantum to the renderer and account for actual AudioContext rate. Emscripten supports C/C++ worklets; its documentation also describes a worklet environment without shared-memory dependency. [Emscripten AudioWorklets](https://emscripten.org/docs/api_reference/wasm_audio_worklets.html), [Web Audio specification](https://www.w3.org/TR/webaudio/#AudioWorklet).

- **Browser instrument:** two local songs, all eight stems and both transport clocks; selected deck determines the four visible physical controls. A/B panels expose song names, timelines, effect choices and available edits. New browser-only stretch/sync features are labeled as extended features rather than claimed to be current firmware.
- **Connected monitor:** physical telemetry and the actual USB mix remain authoritative. Do not also run local copies audibly; that creates doubled audio and drift. Library load commands are explicit. A browser echo/reverb applied after a mixed USB input is a master effect, not an individual physical-stem effect.
- **Optional future controller mode:** requires a documented bidirectional firmware control protocol with accepted-state replies, ownership and timestamp handling. Telemetry support alone does not make this mode exist. Do not send stored local fader positions on reconnect.

Eight stereo float stems of ten minutes at 44.1 kHz occupy approximately **1.69 GB** before originals, separated outputs and DSP buffers. Simply duplicating the current `AudioEngine` is not an adequate mobile design. Stream decoded/encoded chunks from local storage using bounded prefetch, release separation buffers, cap memory with a clear file limit, and cache waveforms as peak summaries. Use transferable chunks when shared memory is unavailable; feature-detect any isolation-dependent fast path. Processing capability and USB browser support are separate feature checks.

Acceptance is exact sample/state comparison against native C for compatibility mode plus browser tests for eight simultaneous stems, gain ramps, loops, pause/cue, pickup, all effects, recording, non-unity rates, suspend/resume and disconnection. The actual MCU's timing failures are not meaningfully emulated by slowing a browser timer. Extended pitch tests instead check timing alignment, pitch error, transients and stereo image.

## 4. Song titles without wiping the library

### Preserve the existing layout

Current layout constants are authoritative for this format:

| Region | Existing purpose |
| --- | --- |
| LBA 0–1 | 1,024-byte slot metadata image; currently full |
| LBA 2 | Legacy grid extensions; preserve even if Bonsai does not use them |
| LBA 3–5 | X3 per-track exact-length/codec metadata, including reserved bytes |
| LBA 6–4095 | Gap before audio; availability must be established, not assumed from a name |
| LBA 4096 onward | 16 slots × 4 stems × 86,016 sectors; ends before LBA 5,509,120 |

The fixed library extent is about 2.82 GB, including the initial gap. Capacity beyond it is not automatically safe scratch: previous firmware may have left meaningful data there, and the current transfer protocol deliberately bounds access. Never change `META_MAGIC`, `TRACK_BLOCKS`, `SLOT0_BLOCK`, slot count, or existing offsets just to add names. Never put text into X3 `rsv`, pan/flags, or the apparently unused tail without a versioned allocation contract. Keep full-image preservation in the existing read-modify-write path.

### First, names without any device write

Store a local catalog keyed by song **content identity**, not only port name, VID/PID or slot number. Preserve a user-editable original title during import, and retain the prepared upload's full audio digest and immutable song ID. Display `Song N` until an existing slot has been reliably associated with its source. A manually supplied title can be stored as such; do not infer it from the current deck number or another browser's cache.

Use IndexedDB for the catalog and provide a small export/import manifest. Browser storage may be evicted, is origin-specific and does not travel with the player; this first stage must be described as local names. For the old uploaded songs, the existing prepared manifest is a possible source only after content verification. A header/length or a few sector matches are not a cryptographic proof that two full songs match.

### Then, optional device metadata sidecar

Add a separately versioned Bonsai metadata extension rather than migrating audio. A suggested first version is two **8 KiB banks**, each containing a bounded header and sixteen fixed-size records. Fields include immutable song ID, content SHA-256, slot-layout fingerprint, UTF-8 title, optional artist, native rate/frame length, analysis version, fixed-tempo grid anchor/BPM, optional key/mode/confidence, flags and record version. Set explicit byte caps, reject malformed UTF-8/lengths, escape JSON, and render titles with `textContent`.

The exact LBA allocation is a **proposal**, not authorized by this document: choose unused, aligned banks within the pre-audio gap only after inventorying all compatible firmware uses and reading/backing up the candidate sectors. Refuse unknown nonblank content. Alternatively allocate beyond the current audio extent only after a capacity/ownership check and a narrowly scoped protocol extension. If neither region is provably available, keep local names; do not initialize the library or overwrite the gap speculatively.

Publish banks as follows:

1. Pause storage-changing operations and acquire the existing single transfer owner. Check layout/device/content identity and read the current valid bank(s).
2. Write the inactive bank with a new generation and invalid/incomplete commit marker. Stream a bounded amount at a time; do not allocate a bank-sized second copy on the MCU audio stack.
3. Flush and read back the entire payload, validating bounded schema, checksum and generation.
4. Write a commit trailer last, flush and read it back. On startup accept only a complete bank with matching header/trailer generation and checksum; select the newest valid generation. No pointer in LBA 0–5 needs changing.
5. If interrupted, keep the older valid sidecar or fall back to numbered songs. Audio and ordinary playback must never require this optional metadata to validate.

Two banks improve recovery from a torn sidecar write; they do not prove power-loss atomicity for the eMMC or fix the inherited song-index publication format. Keep them away from known audio/index regions and test interruption at each write boundary before shipping.

Add explicit capability discovery and bounded `list/read/update title` requests; retain old `DDLIB?` compatibility. Return names incrementally or per slot rather than expanding the current small response buffer with unbounded sixteen-song strings. Preserve songs when an older firmware ignores the extension. New upload/delete/record publication must invalidate or update that slot's sidecar identity. An older writer can change audio without changing a recognizable index shape, so an unverified sidecar name must not be treated as authoritative after unknown writes; verify full content when adopting such a slot and retain a Pending/Unknown state until verified.

Variable-tempo beat maps need a later bounded blob format or a site catalog; do not squeeze arbitrary beat arrays into title records. Constant-grid pre-rendered material is sufficient for the first offline prepared-match workflow.

## 5. Factory-accurate geometry

### Evidence and limits

- The owner's `../virtual-sp1/references/IMG_0198.png` and sibling photographs establish the rocker profile, right-side controls, LEDs and top controls. They lack a scale datum, are perspective-distorted and sometimes soft-focused; they cannot establish absolute millimetres or hidden surfaces.
- `references/manual-2.jpg` is an original illustrated front-view reference. It establishes the control arrangement and relative visual shape; it is not a dimensioned mechanical drawing. Its labels describe stock firmware, not Bonsai control behavior.
- `references/bottom-online.jpg` shows the USB opening and two round openings. It is a visual reference, not proof that nominal connector sizes equal the enclosure's opening dimensions.
- `public/device.js` uses procedural unit dimensions and `control-height.js` contains user-approved multipliers: body 97%, wheel 143%, PLAY/FUNCTION 221%, track buttons 150%. These are tuned visual settings, not factory measurements. Preserve this approved model until a measured replacement is reviewed.
- Teenage Engineering's public downloads page has a CAD section but no SP-1 CAD/model was found there or in the official-domain searches performed for this review. This is a bounded search result, not proof no drawing exists. [Official downloads](https://teenage.engineering/downloads).
- Tim Knapen's repository does contain [`step/TE-SP-1_dev_enclosure.step`](https://github.com/timknapen/SP-1-dev/blob/main/step/TE-SP-1_dev_enclosure.step), introduced by [commit 64bdf13](https://github.com/timknapen/SP-1-dev/commit/64bdf13c651c8e27c31e8e8ac33d44ee896cd9b7). Its header names a development enclosure and two shell solids, with an Autodesk export timestamp. It has no factory provenance or tolerance statement. It may help as a community mechanical reference after inspection, but must not be relabeled factory SP-1 CAD.

### Measurements required from this player

Use millimetres, zeroed calipers, and a defined coordinate system: looking at the front, X from the left casing edge, Y from the bottom casing edge, Z from the front shell surface. Distinguish casing from protruding controls. For each measurement record the tool resolution and repeatability; do not invent tolerances from a photo.

| Feature | Required measurements |
| --- | --- |
| Overall casing | Width, height, thickness excluding controls; metal-panel and plastic-spine widths; seam position; top/bottom and left/right corner radii or radius-gauge comparisons; front/back edge bevels |
| Front faders | Four cap center X values; center Y at each travel endpoint; cap diameter and projection; slot width/length/end radius; collar dimensions |
| Track buttons/lights | Each button center X/Y, width, length, projection, corner radius; LED center coordinates and visible diameter |
| PLAY / FUNCTION | Center along right edge and through thickness; button width/length/projection at rest; recess dimensions; inset anchoring; press travel if safely measurable |
| Rocker | Recess width/length/depth and corner radius; center; flat face width/length; projection at rest; shoulder break positions/angles; end travel and pivot behavior; aperture center/diameter below it |
| Top edge | Both volume-cap centers, diameter/projection; ten grille hole centers/diameters; distances to seam and front/back datum |
| Right lights | Four LED centers/diameters/spacing and separate larger aperture; distances from PLAY/FUNCTION and front/back datum |
| Bottom and rear | New square-on bottom and rear photographs with ruler in the same plane; port opening centers, widths/heights/diameters/recesses; rear screws and markings. Do not use nominal USB/jack dimensions as casing measurements |

Photograph front/back/each edge square-on with a scale in the same plane, then oblique views for relief. Keep the device away from wide-angle close-focus distortion. No disassembly is needed for this exterior model. If exact button travel cannot be measured safely, leave it unknown instead of exercising force or inventing an animation depth.

Create one measured parameter file used by both Three.js and the SVG guide, with source, date, unit and uncertainty for every value. Produce orthographic silhouettes and six matching camera views for approval. Compare calibrated dimensions and landmarks before material/light polishing. The truthful finished label is “measured reconstruction of this SP-1” with its tolerance; “factory exact” requires actual factory data.

## Acceptance gates before claiming completion

1. **Physical audio baseline:** fixed song pair, confirmed all eight nonzero gains/no mutes, separate dry/effect/USB/record cases, non-unity speeds, control movement and deck loads. Record per-interval underrun/error deltas, callback distribution/max and stack margin. Capture actual audio as well as counters. A proposed engineering target is callback worst case below 80% of the 5.333 ms deadline in sustained worst-case tests; this is a target, not a present measurement.
2. **Pitch/tempo:** test tones and music prove requested pitch shift while output duration stays exact; beat-map impulses prove alignment, including ratio changes and loops; stereo channels retain coherence. Tempo sync and key matching are named separately in UI and documentation.
3. **Browser completeness:** eight independent voices, two independent songs/timelines, exact compatibility-mode sample/state comparisons, full gesture/LED behavior, no double-audio on physical connection, and bounded memory on a representative mobile device.
4. **Metadata safety:** byte-for-byte preservation of original index, X3 and unrelated audio; malformed/oversized title rejection; interruption at every bank publication stage; slot reuse, old-writer changes, reconnect and cross-browser tests; unknown data refused.
5. **Geometry:** actual measurement table plus calibrated front/back/edge comparisons; no unsupported factory claim. User-approved protrusion settings remain the comparison baseline until the measured version is accepted.

## Delivery sequence

Implement the browser two-deck compatibility engine and local title catalog while physical reliability is being measured. Add the optional title sidecar only after its space ownership and interruption behavior are demonstrated. Evaluate and license a browser stretch/analysis path, then deliver editable sync and independent stem pitch there. Add prepared-match upload and a sample-clock quantized device launch, preserving all existing songs. Finally explore constrained live physical pitch only if the measured CPU/RAM budget supports it. Build the measured 3D replacement as an independent asset track when the missing dimensions arrive.

The existing illustrated guide's setup, effect-amount persistence and pause-both-before-save instructions were rechecked during this review and match the present control code. No guide edit was needed for those actions.
