# Bonsai 8 site

The browser companion for Bonsai 8 custom SP-1 firmware. Live site: https://sp-1.xyz/. Firmware repository: https://github.com/AWD-tech/bonsai-8 (private).

Publish only `public/`, never the workspace root. The deployed files include a candidate firmware application and its release manifest, public libraries and licenses, and the user manual. They must not include user audio, device logs, raw reference photographs, local credentials, or temporary test fixtures.

## Run and test

```sh
portless run --name bonsai8 npm run dev
npm test
```

Portless supplies `PORT`. Use the URL it prints; the proxy must be running. The application requires no build step. Three.js and fflate are vendored. Fonts, ONNX Runtime and the pinned HT-Demucs model download from their public CDNs. No audio backend or API key is required.

## Browser and physical player

- Procedural Three.js WebGL 2 model with the approved dimensions, buttons, wheel, orbit and views. It is a visual approximation, not manufacturer CAD.
- Browser-only four-stem mixer, mute/solo, loop, level, speed/pitch and seek. Original synthesized demo, individual stem import, local HT-Demucs separation and WAV stem ZIP export.
- Web Serial status and timestamped physical controls/LED mirroring. USB audio monitoring selects only the named Bonsai 8 or older SP-1 Dual Deck audio input. It never plays an unidentified microphone.
- `On your player` lists occupied slots with durations and track counts. Firmware has no persisted song names yet, so unnamed slots appear as Song N.
- Independent Load A / Load B selects any occupied slot without stopping the other deck. Loading cues the target; it does not autoplay or change the physically selected deck.
- Explicit selected-song deletion preserves unrelated index records and extended metadata. It does not erase or initialize the whole library.
- Existing imported/separated browser stems upload to an empty slot in 24 kHz stereo P14S format. One acknowledged, verified sector is sent at a time. Interrupted uploads save checkpoints and compare the entire saved prefix on reconnect before resuming. Audio and extended metadata are verified before the final song index is published.
- On-device P14S/P16M audio exports to stereo WAV: one file for a single-track recording, or a ZIP for a multitrack song. Unsupported or corrupt codecs fail closed.
- The firmware installer downloads only the published Bonsai 8 release, checks its SHA-256, size, application address and vectors, then requests the bootloader port after an explicit install click. It updates only the application region. Existing songs are not written. The bootloader supplies CRC-valid acknowledgements, not application readback.

When physically connected, mixer sliders/buttons display physical settings; they do not pretend to send unsupported remote volume or pitch commands. Change those on the player. File upload, deletion and export pause both decks. Read-only library listing does not enter transfer mode. A single serial owner suspends polling during exclusive operations.

Recording happens on the player with no computer required. Firmware reports recording/save state; library mutations and loading are blocked until the recording is saved. USB polling allows the longer storage-cache flush. Refresh discovers completed recordings. See the included manual for physical gestures and the candidate's limits.

## Firmware release

`public/firmware/manifest.json` identifies the binary with `product`, `version`, `status`, `bytes`, `sha256`, `application_address` and `file`. `product` is `Bonsai 8`, address is 131072 (`0x20000`), and `status` is `candidate` until hardware validation is complete. Never label a candidate verified based only on compilation or host tests.

Bonsai 8 0.4 is a **test candidate**. Eight-stem playback with effects, USB audio and recording needs physical testing. Echo/reverb use a deliberately limited dark mono effect path. There is no automatic beat matching, key matching, independent pitch shift or time stretching: the wheel changes the selected deck's pitch and speed together.

The normal runtime protocol uses `DDSTAT?`, `DDMIR?`, `DDLIB?` and `DDLOAD <deck 0/1> <slot 1..16>` newline commands. Transfer mode uses `SP1XFER!P`, strictly validates the existing 512/16/4/4096/86016/SE3A layout, and uses R/B/F/X operations. No initialization command is exposed. Legacy monitoring remains optional; older firmware cannot use the new library UI.

## Validation and limits

`npm test` covers geometry, mirroring, audio selection, codecs, WAVs, targeted metadata changes, transfer exclusivity and framing, failed writes and publication ordering, checkpoint mismatch, recording timeouts and updater framing/bounds. Browser QA used clearly labeled synthetic song slots and never touched hardware. See `tests/VERIFICATION.md` for exact evidence and remaining limits.

Local inference is demanding and may take several minutes. Browser import is capped at 200 MiB and ten minutes; player uploads are capped at the device's roughly eight-minute slot capacity. Separation uses a WASM worker, not WebGPU. Model/runtime downloads start only when separation is requested. Browser decode support varies by format. USB/browser audio adds latency. Interrupted or power-lost writes cannot be made fully atomic by the inherited storage format, which has no journal; the site checks writes and delays publication to reduce exposure.

## Credits

See `public/credits.txt` and `public/vendor/*LICENSE.txt`. Original MIT credits are retained. The CRC table comes from SP-1 Tape Looper; bootloader framing follows the existing Solderless updater protocol.

## User guidance and icons

`public/guide.html` is the illustrated, mobile-friendly guide. The downloadable manual is generated from the firmware repository's `USER_MANUAL.md`. Use SVG paths for interface icons; do not use emoji or Unicode play/pause glyphs. Keep both transport states accessible by updating their button label.
