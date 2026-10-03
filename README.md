# Bonsai 8

Custom firmware for the SP-1: two independently selected songs, four stems per song, and eight stems mixed on the player. The four faders control the selected deck; switching decks keeps the other song's settings. Playback does not need a computer.

**[Read the complete user manual](USER_MANUAL.md)**, [follow the illustrated guide](https://sp-1.xyz/guide.html), or [download the printable Bonsai 8 manual](output/pdf/Bonsai-8-User-Manual.pdf). It explains mixing vocals from one song over another song's instruments, browsing the whole library, pickup, lights, volume, pitch, the candidate filter controls, standalone recording and the companion site.

Private development: [AWD-tech/bonsai-8](https://github.com/AWD-tech/bonsai-8). Companion website: [Bonsai 8](https://sp-1.xyz/). See [CONTRIBUTING.md](CONTRIBUTING.md) for branches and pull requests.

## Release status

**`bonsai-8-0.4.4` is installed on the owner's device**, flashed on October 3 from 09:39:54 to 09:40:02 UTC (05:40 EDT), with runtime identity confirmed at 09:41:12 UTC. The bootloader acknowledged 27 application pages, 469 chunks and two CRC-valid finalizations. Runtime reports `effects=["filter"]` and the same four song entries with unchanged frame counts. No library writes or flash/audio readback were performed. This confirms installation and the song index, not reliable playback under load.

**0.4.4 still has a USB/mirroring playback failure.** All eight dry stems passed a stable 20.054-second window with zero dropouts. USB capture alone also passed 20.049 seconds. Adding mirror polling produced 225/241 new deck underruns in 20.063 seconds while controls, gains and effects stayed unchanged. All filters were bypassed, so removing Echo and Reverb does not resolve the remaining telemetry/load issue. Full-filter and recording checks remain open. Earlier results are preserved in [HARDWARE_TEST.md](HARDWARE_TEST.md). The public installer remains the labeled 0.4.0 candidate, not a verified release. Changing the website does not update a physical player. See [RELEASE_POLICY.md](RELEASE_POLICY.md).

**The installed 0.4.4 remains a candidate awaiting hardware performance verification.** It removes Echo and Reverb and gives each stem an independent high-pass filter. Increasing its amount removes bass and low frequencies while keeping the brighter highs. Hold FUNCTION + a stem button and move that fader to adjust its amount; the rocker has no effect during filter editing. Side light 1 identifies Filter. Amount zero bypasses it. The ARM build is 106,504 bytes, using 230,772 of 262,144 RAM bytes (17,272 bytes less than 0.4.3). SHA256: `cb6e2c1ebb8e5277a0ae693bea5ed0d4bc0a98c4760312a738031bb907fb64ef`. Six C sanitizer suites and 33 Python tests pass. Measured eight-stem USB/mirroring tests are still required before release.

The preceding 0.4.3 diagnostic candidate was built but not flashed. It adds exact-output ring-copy/filter-math optimizations and timing counters for USB capture copying, USB SOF handling and CDC transmission. Its application is 107,520 bytes; RAM use is 248,044 of 262,144 bytes. SHA256: `3a0fc2bb2c58a5de47692a94fda3ec9c680b2d4c6fc76a4a5ec2d58a08245294`. Six C sanitizer suites and 31 distinct Python tests passed for that candidate. Those results do not establish 0.4.4 build size or physical performance.

The site implements browsing the compatible device library, loading any occupied song into either deck, deletion, upload/export and an application-only browser flasher. Firmware implements standalone recording. The 0.4.4 candidate retains one persistent high-pass filter per stem; older 0.4.0–0.4.3 builds also offer Echo and Reverb. Hardware checks for browsing under playback load, browser flashing and sustained recording/save/reboot/export remain pending. Uploaded filenames are known only during the current connection on this firmware; reconnecting falls back to Song N because the player does not yet report trustworthy persistent title/content identity. The four-song library had independent audio-sector sampling after the earlier 0.3.1 update; later index checks do not repeat that audio verification.

The published companion site now includes two independent browser decks and **prepared matching**: editable BPM/beat/key estimates, master-deck selection, tempo stretching that preserves pitch, and independent stem pitch changes that preserve duration. It prepares new audio buffers for export/upload and can schedule their first beats together. Synthetic-demo analysis, +7-semitone vocal preparation, aligned launch and four-WAV export were checked in the real browser; 138 site tests pass. Analysis can be wrong and requires listening/grid correction. This does not provide continuing beat lock or live physical matching.

The physical SP-1 still has **no automatic beat/key synchronization or independent per-stem pitch**. Within each song, four stems share one playhead. Between songs, tempo and launch timing are manual; the rocker changes the selected deck's speed and pitch together. Each stem volume is independent and master volume controls their combined output. Physical pitch and durable-title experiments are isolated prototypes, not installed features.

Based on [chattock/sp1-tape-looper](https://github.com/chattock/sp1-tape-looper), pinned at `44ba1ecbec6c844dba7f47eacee94c53af8ab10d`. Original MIT notice and board attribution are retained. `README-upstream.md` and the root `sp1_looper.bin` are upstream references, not this release. The upstream README only has its download-link icon removed.

## Controls — 0.4.4 candidate

The table describes the installed 0.4.4 filter-only mapping. Older 0.4.0–0.4.3 versions cycled Filter, Echo and Reverb with the rocker during effect editing, with side lights 1/2/3.

| Control | Action |
| --- | --- |
| Hold FUNCTION 1.5 seconds | Power on/off |
| Tap FUNCTION | Switch fader/button control between A and B |
| Four faders | Selected song's four stem levels |
| Tap track button | Mute/unmute that stem |
| Hold track button | Solo while held; restore previous mutes on release |
| Tap PLAY | Play/pause selected song |
| Hold PLAY 450 ms | Cue selected song to its start, paused |
| FUNCTION + PLAY | Start both decks, or pause both if both are playing |
| Rocker | Selected deck's tape speed/pitch, 1% steps, 0.5×–1.25× |
| FUNCTION + rocker | Previous/next occupied song, wrapping across all 16 slots; selected deck stops |
| Volume +/− | Master output level; hold to repeat |
| FUNCTION + both volume buttons, held 1 second | Toggle standalone mix recording to the first empty slot; pause both decks afterward to save |
| Hold FUNCTION + one track button | Edit that stem's filter: its fader changes amount, rocker has no effect; side light 1 identifies Filter; settings stay after release |
| Hold Track 1 + Track 4 for 3 seconds | Bootloader recovery, including in charging standby |

Deck A is shown by the first status LED, B by the fourth. The middle LEDs indicate A/B playback. Holding FUNCTION shows song position/bank using the four LEDs. Blinking track LEDs mean the fader needs pickup: move it through the saved level before it takes control. B starts with its faders at zero. Master volume starts low.

The first two occupied song slots load at startup, both paused. They are startup choices, not a fixed pair: either deck can load any occupied slot. To keep A playing while finding a different B song, tap FUNCTION to select B, hold FUNCTION and tap the rocker toward next or previous, then release and tap it again for each song. Empty slots are skipped and the library wraps. The selected deck loads paused; tap PLAY to audition it in the combined output. There is no separate headphone cue bus. Cue and speed are independent per song. This build does not automatically align beats or stretch time without changing pitch. Song audio persists; deck selection, gains, mutes, pitch and transport state are session-only. The candidate records the post-master A+B mix to stem 1 of the first empty slot. After stopping the take, pause both decks: cache flush and complete audio readback happen before publication and can take time. Transport is blocked during final save. Recording overflow/write failure leaves the take unpublished. Filter settings persist after releasing their edit gesture, but reset on power cycling. Set the amount to zero to bypass. Bluetooth, MIDI clock and host-to-device USB audio monitoring remain absent.

## Storage and uploading

**Flashing does not format the device.** Unknown stock/2.x storage is preserved and write-locked; alternating status LEDs indicate this state. It cannot play the original stock album format. Back up existing songs with the appropriate current-firmware tool before initializing a new library. An explicit initialization replaces the old song index.

Compatible playback formats are Tape Looper 3.x P14S stereo and P16M mono (24 kHz source; 48 kHz stereo output). Older ADPCM takes are rejected rather than played as noise. Re-export/re-upload those takes. Existing compatible audio is read without rewriting its metadata. Ordinary playback controls do not write the card. The explicit recording gesture writes only a reserved empty slot and publishes it after verification.

Run commands from this directory with the device powered on normally. Bootloader mode is for flashing only. The setup used here has Python at `.build-env/python/bin/python`; any Python 3.10+ with `pyserial` works. Uploading also needs `ffmpeg` on PATH.

```sh
.build-env/python/bin/python tools/sp1.py ports
.build-env/python/bin/python tools/sp1.py status
.build-env/python/bin/python tools/sp1.py list
```

Only after backing up, to explicitly replace the current library with an empty compatible one:

```sh
.build-env/python/bin/python tools/sp1.py init --erase-all-songs
```

Upload four separated stems for song A and then four for song B. The existing virtual SP-1's exported WAVs can be used. This firmware plays prepared stems; stem separation remains on the computer.

```sh
.build-env/python/bin/python tools/sp1.py upload --slot 1 vocals.wav drums.wav bass.wav other.wav
.build-env/python/bin/python tools/sp1.py upload --slot 2 vocals-b.wav drums-b.wav bass-b.wav other-b.wav
```

Version 0.2 adds verified writes: the device reads written data back and compares every byte before acknowledging it. Use the included host tool: it sends one sector per command and waits for acknowledgement, despite the firmware advertising eight-sector capacity. Larger requests overflow the receive ring; paced multi-sector writes also failed a longer hardware test. The host falls back to sector readback with version 0.1.

Each song's stems start together; shorter stems receive silence to match the longest. The uploader resamples to 24 kHz stereo and verifies every written audio sector by default. It refuses occupied slots unless `--replace` is passed. Replacing invalidates that slot before writing; an interrupted replacement remains empty instead of playing a partly overwritten song. Other song metadata is retained. Transfers pause both decks. Temporary encoded files are removed afterward.

## Hardware test

1. Back up the current firmware/song library using the tools for the firmware currently on your unit. Keep a known-good recovery binary.
2. Build/package the candidate and verify its manifest/checksum. Use the matching updater and the exact candidate binary named by its manifest, never the root upstream reference binary. The application-only site flasher is a candidate feature awaiting hardware verification; the known recovery route is the SP-1 utility at [Solderless](https://solderless.engineering). Disconnect other serial owners, hold Track 1 + Track 4 for three seconds and keep USB connected. The installed test firmware was confirmed as 0.4.4 on October 3; its hardware performance gates remain open.
3. Replug, then hold FUNCTION 1.5 seconds. First test power-off and Track 1 + Track 4 recovery before initializing or uploading audio.
4. Check `status`: storage, reset reason, fault marker, CRC/read errors and the audio deadline are reported. A normal no-fault marker is `4294967295`.
5. Load two different songs with four recognizable stems each. Test each deck separately at low volume, then both at 1×. Switch A/B and confirm pickup, mute/solo and pause affect only the chosen deck. Test headphones and speaker auto-mute.
6. Start with effects bypassed, then repeat the measurements with the intended effects and with recording active. Additional processing and simultaneous storage writes need their own passing performance results. Move all eight stored stem gains above zero, unmute all eight, then run `tools/check_playback.py --seconds 20` with both decks running and controls untouched. Repeat with its `--mirror` option and a capture from the named SP-1 USB audio input. A zero-gain or paused deck is not an eight-stem test. For a longer log, run `tools/sp1.py status --seconds 180`. After startup, `underruns`, `read_errors`, `bad_blocks`, `crc_errors`, and `i2s_errors` must stay unchanged; `audio_us_max` must stay below the 5,333 μs output-block deadline. Test 0.5× and 1.25× afterward. Save the JSON log. `clips` means lower stem/master levels.
7. Test a song change on one deck while the other plays, two full song loop boundaries, USB unplug playback, power cycling, transfer interruption, and recovery again.

If playback starves, the entire affected deck pauses its timeline and fades briefly, so its stems cannot drift apart. An error counter records this; it is a test failure, not a claim of seamless playback. Unsupported/corrupt blocks are silent and reported.

## Build and verification

Build environment: Zephyr 4.3.1 (`75f67d766726351b30199f9a2bf55803d717a3be`), Zephyr SDK 0.17.4, CMake 3.31.10, Python 3.11. `tools/setup.py` restores the exact dependency sources; it does not download/install the SDK. Set `ZEPHYR_SDK_INSTALL_DIR` if the SDK is elsewhere.

```sh
python3.11 tools/setup.py
./tools/build.sh
./tools/test.sh
```

The board links the application at `0x20000`, leaving the bootloader untouched. The raw `build/zephyr/zephyr.bin` already starts at the application vector table: **do not strip another 0x20000 bytes**. The release manifest records its checksum, vectors, memory use and source hashes.

The C mixer is shared between desktop tests and firmware. Bonsai 8 firmware hooks replace the upstream mixer, storage task and controls while retaining codec/I2S initialization, watchdog, charging, LED driver, headphone detection and recovery. The upstream four-track engine is removed by compiler/linker dead-code elimination; no second copy of its audio rings is present in the binary. Reused upstream source still emits unused-code/style warnings; the new engine/adapter compile without warnings.
