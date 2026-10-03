# Bonsai 8

Custom firmware for the SP-1: two independently selected songs, four stems per song, and eight stems mixed on the player. The four faders control the selected deck; switching decks keeps the other song's settings. Playback does not need a computer.

**[Read the complete user manual](USER_MANUAL.md)**, [follow the illustrated guide](https://sp-1.xyz/guide.html), or [download the printable Bonsai 8 manual](output/pdf/Bonsai-8-User-Manual.pdf). It explains mixing vocals from one song over another song's instruments, browsing the whole library, pickup, lights, volume, pitch, standalone recording and the companion site.

Private development: [AWD-tech/bonsai-8](https://github.com/AWD-tech/bonsai-8). Companion website: [Bonsai 8](https://sp-1.xyz/). See [CONTRIBUTING.md](CONTRIBUTING.md) for branches and pull requests.

## Release status

**0.4.5 is the current hardware-test candidate.** The new candidate removes all effects and filters. It replaces bytewise USB control replies with bounded, batched transmission, preserves queued button/light events under backpressure, and guards recording against a second trigger before the complete shortcut is released. Site changes reject mismatched song-load acknowledgements and recover correctly from recording failures.

Build size, test results and installed-version evidence are recorded in [HARDWARE_TEST.md](HARDWARE_TEST.md). Host checks do not establish physical playback quality. CDC timing now measures enqueue/callback work; compare dropout and refill results directly with prior builds.

On installed **0.4.4**, eight dry stems and USB audio alone passed short stable tests. Combined USB audio and mirroring caused 225/241 new deck underruns in 20.063 seconds with all effects bypassed. This remains the hardware regression to close. A 3.173-second mix saved as Song 5 and all its encoded audio exported successfully as a non-silent WAV; the original four entries remain unchanged. Sustained recording, its early-stop cause and browser export are not yet verified. Exact evidence and earlier releases are recorded in [HARDWARE_TEST.md](HARDWARE_TEST.md). The public installer remains the labeled 0.4.0 candidate, not a verified release. Opening the site does not flash the player. See [RELEASE_POLICY.md](RELEASE_POLICY.md).

The site can list compatible device songs, load any occupied slot into A or B, delete, upload, export and install application-only firmware. Hardware checks for browsing during playback, browser flashing and sustained recording/save/restart/export remain pending. Uploaded names are local to the current connection; reconnecting falls back to Song N because durable titles and content identity are not yet reported by firmware.

The companion site also provides **prepared matching**: editable BPM/beat/key estimates, master-deck selection, tempo stretching that preserves pitch and independent stem pitch changes that preserve duration. It creates new buffers for export/upload and can schedule their first beats together. Analysis requires listening and grid correction. It does not provide continuing beat lock or live matching on a disconnected player.

The 0.4.5 candidate adds live pitch-preserving deck tempo and beat following from user-tapped grids. Tap at least four quarter beats for each playing song, then make the selected deck follow the other. Grids are session-only and reset when a song loads. This is not automatic BPM detection or key matching. Wide tempo changes can repeat or smear transients; real-device performance and listening checks remain release gates.

Based on [chattock/sp1-tape-looper](https://github.com/chattock/sp1-tape-looper), pinned at `44ba1ecbec6c844dba7f47eacee94c53af8ab10d`. Original MIT notice and board attribution are retained. `README-upstream.md` and the root `sp1_looper.bin` are upstream references, not this release. The upstream README only has its download-link icon removed.

## Controls — 0.4.5 candidate

The table describes the effect-free 0.4.5 candidate. The installed 0.4.4 still has its high-pass filter; earlier 0.4.0–0.4.3 builds also include Echo and Reverb.

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
| Rocker | Selected deck's pitch-preserving tempo, 1% steps, 0.5×–1.25×; exits beat following |
| FUNCTION + Track 1 | Tap at least four quarter beats to teach the selected playing song’s grid |
| FUNCTION + Track 2 | Make the selected deck follow the other deck’s taught beat grid |
| FUNCTION + Track 3 + rocker | Nudge the follower’s beat phase earlier/later |
| FUNCTION + rocker | Previous/next occupied song, wrapping across all 16 slots; selected deck stops |
| Volume +/− | Master output level; hold to repeat |
| FUNCTION + both volume buttons, held 1 second | Toggle standalone mix recording to the first empty slot; release all three buttons before repeating, then pause both decks to save |
| Hold Track 1 + Track 4 for 3 seconds | Bootloader recovery, including in charging standby |

Deck A is shown by the first status LED, B by the fourth. The middle LEDs indicate A/B playback. Holding FUNCTION shows song position/bank using the four LEDs. Blinking track LEDs mean the fader needs pickup: move it through the saved level before it takes control. B starts with its faders at zero. Master volume starts low.

The first two occupied song slots load at startup, both paused. They are startup choices, not a fixed pair: either deck can load any occupied slot. To keep A playing while finding a different B song, tap FUNCTION to select B, hold FUNCTION and tap the rocker toward next or previous, then release and tap it again for each song. Empty slots are skipped and the library wraps. The selected deck loads paused; tap PLAY to audition it in the combined output. There is no separate headphone cue bus. Cue and speed are independent per song. Live sync requires valid tapped grids on both playing decks; it gradually aligns their beats without a sudden seek. Song audio persists; deck selection, gains, mutes, tempo, beat grids and transport state are session-only. The candidate records the post-master A+B mix to stem 1 of the first empty slot. After stopping the take, pause both decks: cache flush and complete audio readback happen before publication and can take time. Transport is blocked during final save. Recording overflow/write failure leaves the take unpublished. Bluetooth, MIDI clock and host-to-device USB audio monitoring remain absent.

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

Version 0.2 adds verified writes: the device reads written data back and compares every byte before acknowledging it. Use the included host tool: it sends one sector per command and waits for acknowledgement, despite the firmware advertising eight-sector capacity. Larger requests overflowed the older receive ring; paced multi-sector writes also failed a longer hardware test. The new 0.4.5 backpressure path has host coverage, but its hardware burst-write performance is unverified; keep the existing single-sector host pacing. The host falls back to sector readback with version 0.1.

Each song's stems start together; shorter stems receive silence to match the longest. The uploader resamples to 24 kHz stereo and verifies every written audio sector by default. It refuses occupied slots unless `--replace` is passed. Replacing invalidates that slot before writing; an interrupted replacement remains empty instead of playing a partly overwritten song. Other song metadata is retained. Transfers pause both decks. Temporary encoded files are removed afterward.

## Hardware test

1. Back up the current firmware/song library using the tools for the firmware currently on your unit. Keep a known-good recovery binary.
2. Build/package the candidate and verify its manifest/checksum. Use the matching updater and the exact candidate binary named by its manifest, never the root upstream reference binary. The application-only site flasher is a candidate feature awaiting hardware verification; the known recovery route is the SP-1 utility at [Solderless](https://solderless.engineering). Disconnect other serial owners, hold Track 1 + Track 4 for three seconds and keep USB connected. The installed test firmware was confirmed as 0.4.4 on October 3; its hardware performance gates remain open.
3. Replug, then hold FUNCTION 1.5 seconds. First test power-off and Track 1 + Track 4 recovery before initializing or uploading audio.
4. Check `status`: storage, reset reason, fault marker, CRC/read errors and the audio deadline are reported. A normal no-fault marker is `4294967295`.
5. Load two different songs with four recognizable stems each. Test each deck separately at low volume, then both at 1×. Switch A/B and confirm pickup, mute/solo and pause affect only the chosen deck. Test headphones and speaker auto-mute.
6. Test normal playback, USB audio with mirroring, and recording separately. Simultaneous storage writes need their own passing performance results. Move all eight stored stem gains above zero, unmute all eight, then run `tools/check_playback.py --seconds 20` with both decks running and controls untouched. Repeat with its `--mirror` option and a capture from the named SP-1 USB audio input. A zero-gain or paused deck is not an eight-stem test. For a longer log, run `tools/sp1.py status --seconds 180`. After startup, `underruns`, `read_errors`, `bad_blocks`, `crc_errors`, and `i2s_errors` must stay unchanged; `audio_us_max` must stay below the 5,333 μs output-block deadline. Test 0.5× and 1.25× afterward. Save the JSON log. `clips` means lower stem/master levels.
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
