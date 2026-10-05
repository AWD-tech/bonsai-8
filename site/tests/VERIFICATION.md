# Verification — 2026-09-29

Live deployment: https://placid-shrine-2h5k.here.now/
Permanent, authenticated here.now publish; only `public/` was published.

- `npm test`: 9 passing tests covering COBS boundaries, CRC rejection, input mapping, WAV headers/clipping/interleaving, overlap-add boundaries, shared playback clock, pitch/seek, mute/solo, and fragmented Web Serial replies with teardown.
- In-app Chromium preview: model rendered without console errors; original synthesized demo played; sidebar gain/mute/solo/pitch changed; direct 3D fader drag set vocals to 4%; clicking the physical-model third track button muted Bass.
- Imported a generated 2-second stereo WAV and ran actual HT-Demucs inference. First attempt with full graph optimization aborted; disabling graph optimization and memory-pattern planning resolved it. Four independent stems completed successfully.
- Downloaded `two-second-mix-stems.zip`. Independently parsed all four exported WAVs: stereo, 44,100 Hz, 88,200 frames each. The synthetic pitched input primarily appears in Other, as expected; stems are not four duplicate copies of the input.
- Mobile 390px viewport had document width 390px and no horizontal document overflow.
- Public HTTPS deployment: 3D canvas, app controls and real synthesized demo playback loaded successfully; no browser console warnings/errors observed. Connection instructions and firmware limitations were checked on the live site.
- No physical SP-1 serial device was attached (`/dev/cu.*` exposed only Bluetooth-Incoming-Port and debug-console). Real USB timing/compatibility and firmware-specific boot behavior remain unverified.
- Visual model follows public references; dimensions and unseen surfaces are approximate. Not manufacturer CAD or full stock firmware emulation.

## Visual revision — 2026-09-29

- Compared supplied left/top/right device photos with the original manual scan and six product photographs, including the missing bottom and rear. Details and source links are in `references/ACCURACY.md`. Reference files remain outside the deployed directory.
- Removed the giant shadow receiver; retained self-shadowing and added room lighting for the metal surfaces.
- Checked front, left, right, top, bottom and rear in the browser. Manual vertical dragging reached both poles: the bottom ports are visible without obstruction.
- Direct 3D fader drag changed Vocals from 100% to 32%; clicking Track 2 muted Drums; upper/lower wheel clicks changed pitch 0 → +1 → 0.
- Original demo loaded four stems, played with an advancing clock, and paused successfully. Audio separation and serial implementation are unchanged from the earlier end-to-end verification.
- Computed font families across interface text, controls and dialogs all resolve to DM Sans; the actual font loaded. Requested eyebrow and decorative labels removed.
- Mobile 390 × 844: document width 390, mixer stacks vertically, no horizontal overflow. Viewport override reset after testing.
- Existing nine tests passed. Updated JavaScript modules passed syntax checks. Room-environment blur adjusted within the PMREM sample budget after its warning was observed.
- Real USB device compatibility remains unverified; geometric dimensions and materials remain photographic estimates.
- Final live verification: current version `01M3P0V66NJCHMM8HT8FXTZJPT`, permanent authenticated update. Initially the browser reused old unversioned app/device/CSS assets with new HTML; versioned entry URLs and the device import corrected this. After a normal reload, the new model, DM Sans, removed eyebrow, and bottom-view selector all worked; no new renderer warnings or page errors. Live viewport also had no horizontal overflow. Left the live tab at the default 3D view.

## Angular rocker revision

- Replaced the round drum with a flat face and angled shoulders after user feedback.
- Checked the left view against IMG_0199; direct upper/lower clicks still changed pitch 0 → +1 → 0.
- Module syntax valid; local browser reported no warnings or errors.
- Versioned app/device URLs updated to avoid stale browser assets.

## Matching enclosure corners

- Front and rear metal panels now use the same 0.16 corner radius as the right plastic spine.
- Shortened flat side/top/bottom walls to the corner tangents and added curved corner walls through the enclosure depth. This removes the square geometry that previously filled the left rounded silhouette.
- Preserved top and bottom opening positions while shortening their panels.
- Browser verification: front, left and bottom views render correctly; no warnings/errors. JavaScript syntax checked. Versioned app/device URLs updated.

## 2026-09-30: Dual Deck connection

- Published version `01M3T1NEQMJKY98XNS0AV48EV6` to the existing permanent site.
- The default connection now sends only `DDSTAT?\n` and reads the custom firmware's JSON telemetry. It never enters transfer or bootloader mode. Original COBS monitoring remains an explicit option.
- Selected-deck gains, mute mask, play state, speed and master volume map to the virtual mixer. Browser audio still uses locally loaded files; device audio and playhead are not streamed or synchronized.
- Thirteen Node tests pass, covering fragmented JSON and banners, firmware field validation, deck A/B mapping, read-only requests, busy-port errors, and the existing stock protocol/audio behavior.
- The deployed page loaded successfully and its connection dialog showed the new normal-power instructions. Screenshot: `dual-deck-connection.jpg`.
- End-to-end physical browser mirroring remains pending while the library uploader owns USB. Do not use the browser to connect until upload and final storage verification are finished.

## 2026-09-30: seated control length and model tools

- Corrected the control-length slider: geometry now extends from the fixed inner base, instead of translating the whole part out of its socket. Wheel pivot, fader carriage, button mounts and press/release positions remain unchanged.
- A real Three.js geometry regression reproduced the detached-base problem for all 13 controls before the fix. The same tests now check anchored bases, actual length at 40/100/104/300 percent, and button release positions. All 34 JS tests pass (`seated-controls-results.log`).
- Replaced the model overlay with an in-flow panel below the canvas, grouped into Buttons & knobs and Device. Each setting has a slider and editable percentage. Previous translation values use a different storage key because their meaning changed; overall device-height preference is retained.
- Published permanent version `01M3T9KMQSTKGW79EWM20KXEHM`. Actual live browser verified a 251% Play button, then restored 104%. At the observed 524px viewport, canvas bottom was 543.8px and tools top was 611.3px; no overlap or horizontal overflow. Browser console had no errors/warnings.
- Screenshot: `seated-controls-live.jpg` (exaggerated 251% button used to inspect the fixed socket). Live firmware/audio verification remains pending power-on; this site correction did not access or modify the physical device.

## Approved production model

The user's settings were read through every visible Model tools selector: body height 97%, wheel 143%, PLAY/FUNCTION initially 235%, track buttons 150%, faders and volume buttons 100%. The user then explicitly corrected PLAY and FUNCTION to 221% and requested removal of Model tools. Those final values are recorded in `approved-model-20260930.json` and are the production defaults. Removed the tools markup, styling, handlers and local preference overrides so the approved model applies consistently. All 34 tests pass. Permanent version `01M3TA3SAY5ZEZNXZ017ATCAR2` was published and reloaded in the existing browser; canvas present, tools absent, no browser errors/warnings. Screenshot: `approved-model-final.jpg`.

## Audio connection status correction

The real-app regression reproduced status polling erasing Listen here failures. Audio errors, stopped state and busy permission state now survive subsequent polls. All 35 JS tests pass (audio-status-results.log). Permanent deployment 01M3TASQ8H8TYGSC9SFZ6F746V loaded with app.js?v=20260930-audio-status-07 and no browser warnings/errors. Screenshot: audio-status-live.jpg. The approved model and removal of Model tools are unchanged. Live hardware failure-state verification remains pending reconnection; macOS previously did not expose the installed 0.3 audio input.

## Live 0.3.1 hardware session

After the 0.3.1 update, macOS enumerated SP_1 Dual Deck as a two-channel 48 kHz input. The browser connected to telemetry, showed both decks advancing, then recovered from a USB disconnect and one failed handshake. Listen here subsequently opened the named input, changed to Stop listening, displayed Live stereo mix from SP-1 at 48 kHz, and drew a non-flat live waveform while the physical deck played. Browser errors/warnings were absent. Evidence: live-usb-audio-0.3.1.jpg. Audible listening quality, full LED/button comparison and sustained performance remain unverified; firmware diagnostics recorded playback-buffer underruns requiring investigation.

## Bonsai 8 companion — 2026-10-02

- `npm test`: 52 passing JS tests, including all existing geometry/mirror/audio tests. Output: `bonsai8-results.log`.
- Local preview uses portless at `http://bonsai8.localhost:8080`.
- Browser QA: actual home page and unpublished-firmware disabled state rendered without console errors. `bonsai8-preview.jpg`.
- A temporary, explicitly watermarked synthetic serial fixture exercised the actual app/library UI: all occupied slots displayed; Song 7 loaded into Deck B while A remained Song 1 playing; delete confirmation named only Song 4; occupied slots excluded from upload destination. `bonsai8-library-fixture.jpg`. The fixture files were removed from `public/` afterward.
- Unit/integration coverage: exact P14S/P16M packing/decoding, WAV output, targeted index-only deletion, preserve other slot/X3 metadata, no publication after rejected or post-flush-corrupt sectors, saved-upload prefix mismatch, exclusive serial ownership, fragmented binary replies, strict library/load JSON, recording save timeouts, updater hash/vectors/bounds, real command sequence and bootloader status bytes.
- No physical USB device was opened, changed, erased, uploaded to, or flashed during this website QA. The updater and library tests use in-memory transports. Full physical validation remains pending.
- The website expects a separately supplied release at `public/firmware/manifest.json` plus its `.bin`. Candidate builds are explicitly labeled unverified for eight-stem playback with effects, USB audio, and recording. No release was invented for UI testing.
- Song titles remain `Song N` when firmware returns `title:null`; browser names are not presented as persisted device metadata. On-player single-track recordings export one stereo WAV; multitrack songs export a ZIP of WAV stems.
- File transfers pause both decks. Browsing and loading use separate runtime commands; loading stops/cues only the chosen deck. Connected mixer controls display physical settings and do not pretend to write unsupported remote volume/pitch commands.
