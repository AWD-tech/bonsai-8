# SP-1 Dual Deck — hardware-test candidates

A custom SP-1 firmware with two independent songs and four stems per song. All eight stems stream from the device's eMMC and mix on its nRF52840. The four faders control the selected deck; switching decks keeps the other song playing. No computer is needed during playback.

**[Read the complete user manual](USER_MANUAL.md)** or [download the printable PDF](output/pdf/SP-1-Dual-Deck-User-Manual.pdf). It includes the song 2 vocals over song 1 instruments walkthrough, every physical control, fader pickup, lights, website connection and current limitations. SFX and independent pitch shifting are not implemented.

Private development: [AWD-tech/sp1-dual-deck](https://github.com/AWD-tech/sp1-dual-deck). Companion website: [Virtual SP-1](https://placid-shrine-2h5k.here.now/). See [CONTRIBUTING.md](CONTRIBUTING.md) for branches and pull requests.

Based on [chattock/sp1-tape-looper](https://github.com/chattock/sp1-tape-looper), pinned at `44ba1ecbec6c844dba7f47eacee94c53af8ab10d`. Original MIT notice and board attribution are retained. `README-upstream.md` describes the upstream looper, not this build.

**Installed hardware-test candidate: `dist/sp1-dual-deck-0.3.1.bin`.** The 0.3 update was acknowledged by the physical bootloader at 23:09 UTC on 2026-09-30. Runtime 0.3 was subsequently confirmed by USB bcdDevice 0x0300, physical telemetry negotiation and advancing Deck A playback time. macOS did not create its audio input. The 0.3.1 update was acknowledged at 23:48 UTC and runtime diagnostics confirm 0.3.1. macOS now enumerates its stereo 48 kHz USB audio input. All four songs passed index/extended-metadata checks and 82 sampled post-flush sector comparisons. Browser capture now connects and displays a changing live waveform; audible quality remains unverified. Subsequent use recorded playback-buffer underruns; two-deck performance is under investigation. The root `sp1_looper.bin` is the unchanged upstream reference, not the custom firmware.

Version 0.2 previously ran on this physical SP-1 on 2026-09-30. USB startup works; multi-sector uploads failed sustained testing, so the included host tool uses separately verified single-sector writes. FUNCTION, PLAY, all four faders and volume previously produced state changes on 0.1. The authorized four-song library upload completed at 21:00 UTC. The device index and extended metadata match all sixteen prepared stems; 82 representative audio sectors, including two previously repaired sectors, matched after flush. Two single-sector writes required repair during transfer; their underlying cause remains unresolved. Mac originals remain untouched. The user subsequently confirmed audible playback works on 0.2. Two-deck/eight-stem endurance, sustained playback bandwidth, remaining controls and power-off still need testing. See `HARDWARE_TEST.md` for evidence and limits.

Version 0.3 adds read-only physical telemetry: debounced button holds/releases, physical fader positions independent of pickup, and all eight actual LED PWM duties. A bounded timestamped queue retains short taps between USB polls and reports overflow. The website buffers about 60 ms to replay timing; sampling, USB transport and display refresh add latency. Unclassified resistor-ladder chords, bootloader operation, and power-off while USB is absent are not claimed to mirror. Screen brightness is an approximation of the reported LED duty, not a photometric calibration.

Version 0.3 implements the post-master stereo mix as a **48 kHz / 16-bit USB capture stream**, but macOS did not recognize it as an audio input during the first runtime check. The implementation is intended to provide device audio to the computer, replacing the host-to-device audio endpoint that Dual Deck did not use. The audio thread never waits for the host. A bounded ring and asynchronous 47/48/49-frame USB packets handle clock differences; underruns, overflows and send errors are reported. The website's “Listen here” uses that SP-1 input only, with browser audio processing disabled where supported, and displays a live waveform. Both deck progress bars use device playback sample positions. Audio and visuals have different transport delays; sample-accurate audiovisual alignment has not been measured.

The [virtual SP-1](https://placid-shrine-2h5k.here.now/) retains compatibility with 0.2 mixer telemetry and clearly identifies the missing physical/audio capabilities. The approved dimensions are now permanent defaults: body 97%, wheel 143%, PLAY/FUNCTION 221%, track buttons 150%, faders and volume buttons 100%. Model tools were removed at the user's request. Version 0.3 **runs and provides playback positions; full physical control comparison and successful USB audio capture remain unverified**. Existing songs are preserved by the update; no initialization or audio upload is required.

## Controls

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
| Hold Track 1 + Track 4 for 3 seconds | Bootloader recovery, including in charging standby |

Deck A is shown by the first status LED, B by the fourth. The middle LEDs indicate A/B playback. Holding FUNCTION shows song position/bank using the four LEDs. Blinking track LEDs mean the fader needs pickup: move it through the saved level before it takes control. B starts with its faders at zero. Master volume starts low.

The first two occupied song slots load at startup, both paused. Cue and speed are independent per song. This build does not automatically align beats or stretch time without changing pitch. Song audio persists; deck selection, gains, mutes, pitch and transport state are session-only. Recording, looper effects, Bluetooth, MIDI clock and USB input monitoring are omitted from this first build.

## Storage and uploading

**Flashing does not format the device.** Unknown stock/2.x storage is preserved and write-locked; alternating status LEDs indicate this state. It cannot play the original stock album format. Back up existing songs with the appropriate current-firmware tool before initializing a new library. An explicit initialization replaces the old song index.

Compatible playback formats are Tape Looper 3.x P14S stereo and P16M mono (24 kHz source; 48 kHz stereo output). Older ADPCM takes are rejected rather than played as noise. Re-export/re-upload those takes. Existing compatible audio is read without rewriting its metadata. No writes occur from ordinary playback controls.

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
2. Open [Solderless](https://solderless.engineering), enter the SP-1 firmware utility, connect USB, hold Track 1 + Track 4, and select **`dist/sp1-dual-deck-0.2.bin`**. The first unit has already been flashed; this step is for installation or recovery on another session/device.
3. Replug, then hold FUNCTION 1.5 seconds. First test power-off and Track 1 + Track 4 recovery before initializing or uploading audio.
4. Check `status`: storage, reset reason, fault marker, CRC/read errors and the audio deadline are reported. A normal no-fault marker is `4294967295`.
5. Load two different songs with four recognizable stems each. Test each deck separately at low volume, then both at 1×. Switch A/B and confirm pickup, mute/solo and pause affect only the chosen deck. Test headphones and speaker auto-mute.
6. Run `tools/sp1.py status --seconds 180` during eight-stem playback. After startup, `underruns`, `read_errors`, `bad_blocks`, `crc_errors`, and `i2s_errors` must stay unchanged; `audio_us_max` must stay below the 5,333 μs output-block deadline. Test 0.5× and 1.25× afterward. Save the JSON log. `clips` means lower stem/master levels.
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

The C mixer is shared between desktop tests and firmware. Firmware hooks replace the upstream mixer, storage task and controls while retaining codec/I2S initialization, watchdog, charging, LED driver, headphone detection and recovery. The upstream four-track engine is removed by compiler/linker dead-code elimination; no second copy of its audio rings is present in the binary. Reused upstream source still emits unused-code/style warnings; the new engine/adapter compile without warnings.
