# SP-1 Dual Deck

User manual - 30 September 2026

Two songs. Four stems each. One physical player.

Website: [Virtual SP-1](https://placid-shrine-2h5k.here.now/)

Private development repository: [AWD-tech/sp1-dual-deck](https://github.com/AWD-tech/sp1-dual-deck)

## 1. Start here

This manual describes the custom **SP-1 Dual Deck** firmware, not the original SP-1 firmware or the upstream Tape Looper controls. It covers the shared device controls in versions 0.2, 0.3 and the 0.3.1 candidate.

**Installed version: 0.3.1.** The update completed on 30 September 2026. Runtime diagnostics identify 0.3.1, all four uploaded songs passed sampled post-update verification, and macOS now recognizes its stereo 48 kHz USB audio input. Audible browser capture is still being checked. Playback-buffer underruns have been recorded during use; two-deck performance needs further work. Changing repository files does not change the firmware on your player.

The firmware loads two songs into **Deck A** and **Deck B**. Each song has four stems. Both decks can play together, but the physical faders and track buttons control only the selected deck. Tapping FUNCTION switches which deck you control; the other keeps its current mix and transport state.

In this library, the stem order is **1 Vocals, 2 Drums, 3 Bass, 4 Other**. Other contains the remaining instruments. The device does not recognize instruments by sound: stem order comes from the order used when uploading.

For ordinary playback:

1. Hold FUNCTION for about 1.5 seconds to power on.
2. Deck A starts selected, with the first occupied song slot. Deck B loads the second occupied slot. Both start paused.
3. Keep the master level low initially. Move Deck A's faders through their stored levels to pick them up, then set the mix.
4. Tap and release PLAY to start the selected song. Tap again to pause.
5. Tap and release FUNCTION to switch to the other deck.

At a fresh start, Deck A stem levels are 100%, Deck B stem levels are 0%, both speeds are 1x, and master volume is 25%. Fader pickup prevents an immediate jump when the physical fader and stored level differ.

**No SFX menu exists in this firmware.** See section 6 for available sound controls and missing effects.

## 2. Song 2 vocals over song 1 instruments

This walkthrough starts from a fresh power-on with songs in slots 1 and 2. If you are already mixing, choose those songs on the appropriate decks first and check their existing mutes and levels.

**Set up Deck A: song 1 instruments**

1. Check that Deck A is selected: the first of the four small side status LEDs indicates A. PLAY and FUNCTION are separate side buttons; the four track buttons belong to the four stems.
2. If necessary, hold FUNCTION and tap the rocker in the next/previous direction to select song 1. Release both. Changing songs stops and cues the selected deck.
3. Tap Track 1 once to mute song 1's vocals. From a fresh start it is unmuted, so one tap turns it off. If it is already muted, leave it muted.
4. For faders 2, 3 and 4, move each fully to the maximum end first to pick up its initial 100% level. Then lower them to the desired instrumental balance. A muted vocal fader can remain anywhere.
5. Tap PLAY. Song 1's drums, bass and other instruments should now play.

**Set up Deck B: song 2 vocals**

6. Tap FUNCTION briefly to select Deck B. The fourth small status LED indicates B. Deck A continues playing.
7. Select song 2 with FUNCTION plus a short rocker tap if necessary. Deck B starts with song 2 after a fresh boot when slots 1 and 2 are occupied.
8. Move all four faders fully to the minimum end. This picks up Deck B's initial zero levels. Keep faders 2, 3 and 4 at zero. Raise fader 1 for the vocals, and make sure Track 1 is unmuted. You can also mute Tracks 2-4 to prevent accidentally raising their instruments later.
9. Tap PLAY at the musical point where you want the vocals to enter. You now hear **A: song 1 instruments + B: song 2 vocals**. Adjust Deck B's first fader to balance the vocal.

**Change the vocal pitch over the other song's drums:** keep B selected and move the rocker without FUNCTION. Only B changes speed/pitch; A stays unchanged. This changes all of B's stems together, so keep its non-vocal stems muted or at zero. Pitch and speed move together: preserving vocal timing while changing pitch is not supported.

To rebalance the instruments, tap FUNCTION back to A, pick up its saved fader levels, and adjust them. To stop only the extra vocal, select B and tap PLAY, mute its Track 1, or lower its first fader after pickup.

To start both from the beginning: select A and hold PLAY for at least 0.45 seconds to cue it; select B and do the same. Then hold FUNCTION and tap PLAY once, releasing both. This starts both decks together. It does **not** align their beats or compensate for different intros.

## 3. Complete physical control reference

Short taps act on release. Hold PLAY or a track button for less than 0.45 seconds when you intend a tap. Press FUNCTION first when using a FUNCTION combination, and perform the second action promptly: holding FUNCTION alone for 1.5 seconds powers off.

| Control | What it does |
| --- | --- |
| Hold FUNCTION about 1.5 seconds | Power on or power off. |
| Tap FUNCTION | Switch the selected deck between A and B. |
| Move a fader | Change that stem's level on the selected deck, after pickup. |
| Tap Track 1, 2, 3 or 4 | Mute/unmute that stem on the selected deck. |
| Hold one track button at least 0.45 seconds | Temporarily solo that stem on the selected deck. Release to restore the previous mute pattern. |
| Tap PLAY | Play/pause the selected deck. |
| Hold PLAY at least 0.45 seconds | Cue the selected song to its beginning and leave it paused. |
| FUNCTION + tap PLAY | Start both decks if either is paused; pause both if both are playing. |
| Rocker, faster/forward direction | Increase selected-deck speed by approximately 1% per step. |
| Rocker, slower/back direction | Decrease selected-deck speed by approximately 1% per step. |
| FUNCTION + rocker forward | Load the next occupied song on the selected deck, paused at its beginning. |
| FUNCTION + rocker back | Load the previous occupied song on the selected deck, paused at its beginning. |
| Volume + / - | Raise/lower the master output for both decks together. |
| Hold Track 1 + Track 4 for 3 seconds | Enter the firmware bootloader, including from charging standby. Playback stops. |

Holding a volume button or the rocker without FUNCTION repeats its adjustment after about half a second. FUNCTION plus the rocker changes one song per new press; release and press again for another song.

Solo affects only the selected deck. It does not silence the other deck and does not raise a fader that is at zero. Multiple track-button combinations do not provide additional documented performance functions; Track 1 + Track 4 is reserved for recovery.

Cue and song changes keep the deck's existing speed, gains and mutes. They do not reset its mix. A normal power cycle resets session settings to the startup defaults.

## 4. Fader pickup, lights and song selection

### Fader pickup

Each deck remembers its own four levels during the session. When you switch decks, the fader must reach or cross that deck's saved level before it changes the sound. This prevents the second deck from jumping to the first deck's physical fader positions.

A blinking unmuted track LED indicates pickup is waiting. Move the fader slowly through the saved level until it takes control. If you do not know that level, sweep to one end and then the other; once pickup occurs, further movement changes the audio, so do this with the master low if needed. A muted or missing stem's LED is off even when pickup is waiting.

### Four small side status LEDs

Number these four lamps 1 through 4 in order along the side, excluding the separate small dark opening.

| Normal display | Meaning |
| --- | --- |
| Status LED 1 on | Deck A is selected for the faders/buttons. |
| Status LED 2 on | Deck A's transport is set to playing. |
| Status LED 3 on | Deck B's transport is set to playing. |
| Status LED 4 on | Deck B is selected for the faders/buttons. |

Both middle lamps on means both decks are set to playing. A playing lamp alone does not prove audible output: that deck can have zero levels or muted stems.

While FUNCTION is held, these lamps show the selected song slot instead. A steady lamp gives its position within a group of four; a blinking lamp gives the group. Slots 1-4 use group lamp 1, 5-8 lamp 2, 9-12 lamp 3, and 13-16 lamp 4. If the position lamp and group lamp coincide, the steady light masks the blink. For an exact slot number, use the connected website's deck readout.

### Four track LEDs

An illuminated track LED means that stem exists and is unmuted on the selected deck. It is **not** an audio-level meter and can stay lit with a zero fader or paused song. Pickup makes an otherwise lit lamp blink. During file transfer all four blink. Alternating side-lamp pairs indicate storage is not available in a recognized format.

Song selection skips empty slots and wraps at the end of the library. Each deck can select any occupied slot, including a song also selected on the other deck. Changing one deck's song leaves the other deck's transport command unchanged; uninterrupted transition performance still needs sustained hardware testing.

## 5. Timing, loops and getting a good mix

The four stems within a song share a playhead. They stay together when you pause, cue or change that deck's speed. The two decks have independent playheads and speeds. Songs repeat from their beginning when they reach their uploaded length; the uploader pads shorter stems with silence to that song's longest stem.

There is no beat grid, automatic BPM detection, sync button, quantized launch, loop-length control or arbitrary on-device seek. Start the second song by ear. For a restart, hold PLAY to cue that deck and then tap PLAY at the desired moment. This restarts the whole song, not a selected section.

The rocker changes tape-style speed between **0.5x and 1.25x**. Pitch changes with speed. It does not preserve pitch or musical key. If you know both BPMs, the approximate speed ratio for matching B to A is A's BPM divided by B's BPM; use the rocker to approach that value. Matching BPM does not align the first beat or fix drifting performances.

There is no single crossfader. Fade by adjusting the stems on each deck and switching decks as needed. Start with modest levels, then increase the physical master. If the combined sound distorts, lower stem levels and master. The firmware has fixed mixing headroom, but eight loud stems can still clip.

Inserting headphones is intended to mute the built-in speaker automatically. Headphones receive the same combined stereo output; there is no separate headphone cue bus for privately previewing Deck B.

## 6. SFX and what is not included

**There is no SFX page, button combination or effects mode in Dual Deck 0.3 or the 0.3.1 candidate.** The custom mixer does not expose the upstream Tape Looper effects. Its old guide and firmware file are retained for attribution/reference and do not describe these controls.

Available performance controls are stem volume, mute, temporary solo, play/pause, cue, independent tape speed/pitch and simultaneous two-song mixing.

Reverb, delay, filter sweeps, distortion effects, stutter/slicer, reverse playback, recording/overdub, loop editing, Bluetooth playback, MIDI clock, key lock and automatic beat matching are not implemented. FUNCTION plus a track button does not open an effects menu.

For an effect today, process a stem in your audio editor before uploading. That effect becomes part of the file and cannot be changed independently on the player. SFX can be developed later as a separate firmware feature; they are not hidden behind an existing gesture.

## 7. What the website is

Open [https://placid-shrine-2h5k.here.now/](https://placid-shrine-2h5k.here.now/).

The **Virtual SP-1** is a companion browser instrument with a rotatable 3D model, music separation, a four-stem browser mixer, stem export and a USB monitor for this firmware. here.now hosts the static site. It is not the firmware running inside the physical player.

### Use it without hardware

Choose **Drop a song here** to select audio, or try the studio demo. The site accepts supported WAV, MP3, M4A and FLAC files up to 200 MiB and 10 minutes. Actual decoding depends on the browser. Separation produces Vocals, Drums, Bass and Other locally using a downloaded model. The first separation downloads roughly 158 MiB and can take several minutes. Music is processed on your computer; the site does not send it to a separation server.

Use **Have stems?** to import prepared stems. The browser mixer supports faders, mute/solo, master, speed/pitch and seeking. Drag the 3D player to rotate it or use Camera view to see its sides and bottom. Its geometry uses the approved proportions; Model tools have been removed.

**Export stems .zip** exports the separated/imported stem audio as WAVs. It is not a recording of your live performance, a two-song mixdown, or a direct upload to physical storage. Unzip the WAVs and use the desktop uploader to put them on the player.

The website's local instrument and physical firmware have different controls and limits. A gesture on the unconnected model is not an authoritative guide to a physical firmware button combination.

## 8. Connect, mirror and listen through the site

Use a desktop browser with Web Serial support, such as Chrome or Edge. The player must be powered on in its normal application, **not bootloader mode**. Close any uploader, flashing utility or other serial monitor first; only one connection can own the serial port.

1. Connect a USB data cable and power on the SP-1.
2. Open the website and choose **Connect SP-1**.
3. Keep the **Dual Deck** firmware mode selected, click Connect, and choose the SP-1 serial device in the browser's chooser.
4. Play a song and move physical controls. The site shows the selected deck, song number, mixer settings and both deck time/progress readouts.
5. With firmware 0.3, the mirror also receives physical fader positions, button holds/releases and the actual duty values of all eight LEDs.
6. If the operating system recognizes an **SP-1 Dual Deck** audio input, choose **Listen here** and allow audio-input access. Stop listening to stop computer monitoring; this does not pause the physical player.

The monitor is read-only for Dual Deck: clicking the website does not remotely change the physical song, write storage or flash firmware. Physical controls drive the physical mix. Connecting does not start an unrelated song previously loaded into the browser.

USB audio is the player's combined post-master stereo mix, not eight separate USB stem channels. Physical master changes therefore affect the captured level. The website's ordinary Master slider belongs to its local browser mixer; it is not a separate USB-monitor volume control. Use the computer output volume to adjust listening level locally.

### Current verification limits

Installed 0.3 connected successfully, negotiated mirroring and showed an advancing Deck A song timer. A complete side-by-side comparison of every physical button/light is still pending. The mirror deliberately buffers about 60 ms; USB and screen refresh add more delay. Screen brightness approximates physical LED brightness. Bootloader and disconnected/off states cannot provide a live mirror.

On the tested Mac, 0.3 did not enumerate a usable USB audio input. After flashing 0.3.1, macOS recognizes **SP_1 Dual Deck**, two input channels at 48 kHz. Successful audible browser monitoring and sustained capture still require verification. Do not interpret a moving timer as proof that USB audio is working. No computer microphone is connected to playback as a substitute for the SP-1 input.

The time readouts show each song's source position and reset at its loop boundary. Changing speed changes how quickly those positions advance. They are not beat markers. The live waveform, when capture works, describes the combined audio. Audio and visuals have different transport delays; sample-accurate alignment is not claimed.

## 9. Prepare and upload songs

The physical player plays prepared stems; it does not separate a mixed song on-device. You can use the website to make stems, or export them from an audio editor. Use four files with the same starting point, in this order: **vocals, drums, bass, other**. Keep leading silence needed for alignment.

The library holds up to 16 song slots, with four stems per song. The current uploader limits songs to approximately eight minutes, even though the website accepts up to ten minutes. It converts uploads to 24 kHz stereo P14S; the device outputs a 48 kHz stereo mix. The player also recognizes compatible P16M mono files, but not the old raw-PCM or ADPCM library used by earlier firmware.

Run these commands from the firmware repository. The examples use the project's Python environment; uploading also needs ffmpeg. Disconnect the website serial connection first.

```sh
.build-env/python/bin/python tools/sp1.py ports
.build-env/python/bin/python tools/sp1.py status
.build-env/python/bin/python tools/sp1.py list
```

Upload to an empty slot, substituting your four actual file paths:

```sh
.build-env/python/bin/python tools/sp1.py upload --slot 5 \
  vocals.wav drums.wav bass.wav other.wav
```

Quote file paths containing spaces. To target a specific device when discovery is ambiguous, put `--port /dev/cu.usbmodemXXXX` before the command name. Use the actual port reported by `ports`.

An occupied slot is refused by default. Adding `--replace` explicitly replaces that slot; interruption can leave that song empty or incomplete. Uploading pauses both decks. The uploader verifies written sectors and flushes storage before completion. Wait for success before reconnecting the website.

**The four songs already uploaded to this player do not need initialization or re-uploading for a firmware update.** The destructive `init --erase-all-songs` command replaces the library index. Do not use it to solve a playback, connection or audio-input problem. Unknown storage is preserved and write-locked until an explicit initialization is authorized.

Keep the original source stems on your computer. Song audio persists across power cycles; deck selection, levels, mutes, speed and play positions do not.

## 10. Troubleshooting

| Symptom | What to check |
| --- | --- |
| Song 2 appears to play but is silent | Deck B starts at zero. Select B, move its vocal fader to minimum for pickup, raise it, unmute that stem and check master. |
| A fader seems unresponsive | It is probably waiting for pickup. Cross the selected deck's saved level. Switching decks re-arms pickup. |
| Vocals from both songs are audible | Select A and mute its Track 1. Select B and confirm only its vocal is audible. |
| Solo does not isolate one song | Solo only affects the selected deck. Pause/mute the other deck separately if you want complete isolation. |
| Both songs are playing but drift apart | There is no automatic beat sync. Match speed by ear and cue/start the second song at the right beat. |
| A song starts at the beginning after PLAY | PLAY was held long enough to cue. Use a short tap for pause/resume. |
| FUNCTION turns the unit off | It was held alone for about 1.5 seconds. Tap briefly to switch decks; press the other control promptly for a combination. |
| Both middle status lamps light but there is silence | Check mutes, fader pickup, master, loaded stems and headphones. A playing flag is not an audio meter. |
| Site cannot connect | Power on normally, use a data cable and a Web Serial browser, and close other serial owners. Bootloader mode is for flashing. |
| Site connects but Listen here fails | Check whether the OS lists SP-1 Dual Deck as an audio input. Version 0.3 had a Mac enumeration issue; 0.3.1 now enumerates correctly on the test Mac. Check the installed version, browser permission and selected input. |
| Site audio echoes against the player | The two output paths have different delay. Monitor through one listening path or reduce one path's listening volume. |
| Status LEDs alternate in pairs | Storage is unavailable or its format is unsupported. Inspect diagnostics; do not initialize an existing library blindly. |
| Playback glitches | Hardware testing has recorded playback-buffer underruns. Save a status log with both deck settings; two-deck performance remains under investigation. |
| No SFX controls can be found | SFX are not implemented in this build. There is no hidden effects mode. |

To collect a three-minute diagnostic log without an active website serial connection:

```sh
.build-env/python/bin/python tools/sp1.py status --seconds 180
```

Record which decks/songs were active and what you heard. Diagnostics help locate faults but are not proof of audible correctness on their own.

## 11. Updates, development and credits

Development lives at [AWD-tech/sp1-dual-deck](https://github.com/AWD-tech/sp1-dual-deck), a private repository for source changes, issues and pull requests. See README.md for build instructions and HARDWARE_TEST.md for measured results. Use a feature branch and a pull request for future changes; keep uploaded audio, build environments and account credentials out of commits.

Firmware updates and library uploads are different operations. A firmware update writes the application; it does not intentionally reformat the song library. For an update, disconnect the site's serial connection, hold Track 1 + Track 4 for three seconds with USB connected, and use the release's matching updater and binary. Afterward, hold FUNCTION about 1.5 seconds if the application needs powering on. Reconnect normally and verify the reported version, controls and stored songs.

The root `sp1_looper.bin` and the older Tape Looper documents are upstream references, not the custom Dual Deck release. Do not select a binary merely because its filename contains SP-1. Version 0.3.1 resolves USB-audio enumeration on the test Mac; this is separate from audible-capture and endurance verification.

This firmware is based on chattock/sp1-tape-looper at commit `44ba1ecbec6c844dba7f47eacee94c53af8ab10d`. It retains the hardware initialization and recovery foundations while replacing the active audio/control path with a two-deck mixer. Original MIT license and attribution are retained. It is a custom community build, not an official SP-1 firmware release.

Control instructions were checked against firmware/src/dual_firmware.inc and firmware/src/dual_engine.c. Website behavior was checked against the companion app and its hardware/audio modules. Implemented behavior, software tests and confirmed hardware results are distinguished throughout this manual.
