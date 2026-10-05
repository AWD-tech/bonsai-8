# Bonsai 8

Two songs. Eight stems. Your mix.

User guide / Firmware 0.4.5 candidate and companion site / 3 October 2026

Bonsai 8 turns the SP-1 into a two-deck stem player. Choose a song for **Deck A** and another for **Deck B**. Bring their voices, drums and instruments together, balance their levels, and record the result on the player.

The four faders control one deck at a time. Tap **FUNCTION** to change decks. The other deck keeps playing with the levels you left it at.

**Release status:** This guide describes the **0.4.5 candidate**, which removes all effects and changes USB response handling. Check `HARDWARE_TEST.md` in the project repository for the current installed version and hardware test results. The candidate adds manually tapped beat grids and live pitch-preserving tempo/sync controls. They still need hardware validation; wider tempo changes can blur or repeat transients. This is not a completed release. In historical 0.4.4 tests, eight-stem playback, USB audio alone and mirroring alone passed short tests; USB audio together with mirroring caused dropouts. A short, non-silent **3.17-second recording saved as Song 5 and exported successfully**. Longer recording and combined-load reliability remain unverified. This guide describes the 0.4.5 candidate controls; earlier firmware differs. Check the exact version and test status in the website installer before updating. Opening the website does not update your player.

### Your first playback

1. Hold FUNCTION for about 1.5 seconds to power on. Both decks start paused; A is selected.
2. A loads the first occupied song slot; B loads the second. Start with the master volume low.
3. On A, move the faders to their maximum end to pick up the starting levels, then lower them to taste.
4. Tap and release PLAY to start A. Tap again to pause.
5. Tap FUNCTION to select B. Its levels start at zero: move the faders to minimum, then raise the stems you want.

At power-on, A's stem levels start at 100%, B's at 0%, master at 25%, and both speeds at normal. Songs remain stored; deck selections and mix settings reset when you power off.

### Keep this nearby

Website: [sp-1.xyz](https://sp-1.xyz/)

Alternate address: [placid-shrine-2h5k.here.now](https://placid-shrine-2h5k.here.now/)

Illustrated guide: [sp-1.xyz/guide.html](https://sp-1.xyz/guide.html)

This guide covers mixing, physical controls, lights, recording, the companion site, library management, installation and troubleshooting.

## 01 / Mix two songs

Use this example to put **Song 2's vocals over Song 1's instruments**. The standard stem order is 1 Vocals, 2 Drums, 3 Bass, 4 Other. Imported files must follow that order for these labels to match their sound.

### A carries the instruments

1. Select A with a brief FUNCTION tap if needed. Side light 1 shows A is selected.
2. Hold FUNCTION and tap the side wheel to browse to Song 1. Release both. The chosen song is cued at the beginning, paused.
3. Mute Track 1, the vocal: tap its track button if it is not already muted.
4. Pick up faders 2, 3 and 4 and set the instrumental balance. Tap PLAY.

### B brings in the vocal

5. Tap FUNCTION to select B. Side light 4 shows B is selected; A keeps playing.
6. Hold FUNCTION and tap the wheel to find Song 2. Release both.
7. Bring all B faders to minimum to pick up their initial zero levels. Leave 2, 3 and 4 at zero, or mute them. Raise fader 1 and make sure Track 1 is unmuted.
8. Tap PLAY at the point where you want the vocal to enter. Adjust fader 1 to balance it over A.

To try a different vocal, stay on B and browse to any other occupied slot with FUNCTION + wheel. B stops and cues the new song; A keeps its playback state. Tap PLAY to bring B back in. Empty slots are skipped and browsing wraps at the end. Both decks can also use the same song.

### Teach the beat, then sync

The 0.4.5 candidate uses beats you tap; it does not detect BPM automatically or match musical keys. All four stems on each deck share its tempo and timeline.

1. Play the song on A. Hold **FUNCTION** and tap **Track 1** on at least four steady quarter-note beats. The first tap sets the beat anchor; further taps establish the source BPM. Release the controls.
2. Select B, play its song and repeat **FUNCTION + Track 1** for at least four beats. Keep the tempo steady while tapping. The connected site's live readout shows tap progress and **Source BPM**, before any tempo change.
3. With both decks playing, select the deck you want to follow the other. Hold **FUNCTION** and tap **Track 2**. The selected deck adjusts its tempo while preserving pitch and gradually aligns to the other deck's beat grid.
4. To shift the follower's beat position slightly, hold **FUNCTION + Track 3** and tap the **wheel** in either direction. Listen while adjusting; a grid lock does not guarantee that the musical phrases fit.
5. Move the **wheel alone** to return the selected deck to manual pitch-preserving tempo control. **FUNCTION + wheel** still browses songs.

Grids exist only for the current loaded songs and session. Loading or cueing a song resets its grid: tap it again before syncing. Start both decks and teach both grids before retrying a failed sync. If the tempo ratio is outside the supported range, choose closer tempos; the player must not pretend to lock by clamping the requested rate.

Live stretching can soften attacks or produce repeated/smeared transients, especially at wide tempo ratios. Hardware performance and listening validation are pending. There is no live key matching or independent physical pitch adjustment for each stem; use browser preparation for those pitch changes.

Songs loop at their full length. Hold PLAY for about half a second to cue the selected song at its beginning, paused. FUNCTION + PLAY starts both together when either is paused; that launch alone does not align their beats or intros.

## 02 / Physical controls

A **tap** is a short press and release. PLAY and track-button holds become a different action after about half a second. For a FUNCTION combination, press FUNCTION first, then the other control promptly; holding FUNCTION alone for 1.5 seconds powers off.

| Control | Action |
| --- | --- |
| FUNCTION, tap | Select Deck A or Deck B. |
| FUNCTION, hold 1.5 seconds | Power on or power off. |
| PLAY, tap | Play or pause the selected deck. |
| PLAY, hold about 0.5 seconds | Cue the selected song to the beginning, paused. |
| FUNCTION + PLAY | Start both if either is paused. Pause both if both are playing. |
| A stem's fader | Adjust its volume on the selected deck, after pickup. |
| A track button, tap | Mute or unmute that stem on the selected deck. |
| A track button, hold about 0.5 seconds | Solo that stem on the selected deck until released. |
| Wheel, without FUNCTION | Adjust the selected deck's tempo while preserving pitch; leave sync. |
| FUNCTION + wheel | Browse all occupied songs on the selected deck. |
| Hold FUNCTION + tap Track 1 on 4+ quarter beats | Teach the playing song's beat grid and source BPM for this session. |
| FUNCTION + Track 2, tap | Sync the selected playing deck to the other playing deck, after teaching both grids. |
| Hold FUNCTION + Track 3, tap wheel | Nudge the active follower's beat phase in either direction. |
| Volume + / - | Adjust the combined master volume. |
| FUNCTION + both volume buttons, hold 1 second | Start or stop recording the mix. Release FUNCTION and both volume buttons fully before repeating. |
| Track 1 + Track 4, hold 3 seconds | Enter bootloader mode for a firmware update. |

### Fader pickup

Each deck remembers its four levels during the session. After switching decks, move a fader through that deck's stored level before it takes control. This prevents a sudden jump in the mix. An unmuted track's light blinks while pickup is waiting.

If you do not know the stored level, sweep slowly to one end and back. Once pickup occurs, further movement changes the sound.

Solo affects only the selected deck. It does not silence the other deck or raise a fader that is at zero. Song changes keep the deck's current levels, mutes and speed until power-off.

## 03 / Read the lights

The four small side status lights show which deck you control and which decks are playing. Count them in order along the side; the separate dark opening is not a status light.

| Side light in normal playback | Meaning |
| --- | --- |
| 1 | Deck A is selected. |
| 2 | Deck A is set to play. |
| 3 | Deck B is set to play. |
| 4 | Deck B is selected. |

Both middle lights on means both decks are set to play. A playing light is not a sound meter: muted stems or zero levels can still make that deck silent.

### Track lights

A lit track light means that stem exists and is unmuted on the selected deck. It can stay lit while the song is paused or its fader is at zero. A blinking unmuted track light means its fader is waiting for pickup. Missing or muted stems have no light.

### When the display changes

| Situation | Side-light display |
| --- | --- |
| Hold FUNCTION to browse | Song-slot position and group. See below. |
| Recording | All four blink slowly. |
| Recording stopped; draining or saving | All four blink faster. Pause both decks and keep power on. |
| Recording saved | Normal deck lights return. |
| Recording failed | The outer lights flash. Do not assume the take was saved. |
| Storage unavailable or unsupported | Alternating pairs. Do not erase the library to troubleshoot this. |

During a file transfer, all four track lights blink. Recording indicators take priority over the deck display.

### Song-slot display

While FUNCTION is held, a steady side light gives the slot's position within a group of four. A blinking light gives its group: light 1 for slots 1-4, light 2 for 5-8, light 3 for 9-12, and light 4 for 13-16.

For example, slot 6 shows light 2 steady and light 2 blinking at the same position, so the steady light hides that blink. Use the connected site's A/B readouts when you need an unambiguous song number.

## 04 / Keep the mix clear

The **0.4.5 candidate has no effects**. Filter, Echo and Reverb are removed. The faders control stem volume; there is no effect-editing gesture or effect-choice light pattern.

### Leave room for the second song

1. Begin with one deck. Pick up its faders and set moderate levels.
2. Bring in only the stems you want from the other deck. For a vocal over another song's instruments, keep the second song's drums, bass and other stems muted or at zero.
3. If two parts compete, lower one before raising the other. Two bass lines or two drum patterns can clash even when playback is smooth.
4. Use the volume buttons to set the combined listening level. If the mix distorts, reduce the stem levels and master volume.

There is no separate physical deck crossfader: build fades with the stem levels. Muting a stem silences it without changing its saved fader level.

### Volume and tempo while mixing

The wheel alone changes the selected deck's tempo from **0.5x to 1.25x** while preserving pitch and exits sync. All its stems stay on the same timeline. Wide ratios can produce transient artifacts; the accepted range is not a sound-quality guarantee. Independent stem pitch and key matching are available through browser preparation, not the physical controls.

Headphones carry the same combined mix as the speaker; there is no separate headphone preview deck.

### Before recording

Check both decks' songs, mutes, levels and speed. The take captures the combined output, including the master level, so a silent or very quiet master produces a silent or quiet recording. Leave an empty song slot and keep a copy of your source audio.

## 05 / Record your mix

Record A and B directly on the SP-1, without a computer. The take includes the audible mix, deck tempo changes and master-volume moves. It saves to the **first empty song slot**; at least one of the 16 slots must be free.

### Start, perform, stop

1. Prepare your mix and start playback.
2. Hold FUNCTION and both volume buttons for **one second**, then fully release FUNCTION and both volume buttons. All four side lights blink slowly while recording.
3. Perform with the faders, mutes, deck switch and wheel. Song changes and library transfers are unavailable until the take is finished and saved.
4. Hold FUNCTION and both volume buttons for one second again, then fully release all three controls to stop. The side lights blink faster as the take finishes.

### Pause both decks to save

5. Pause both decks. If both are playing, FUNCTION + PLAY pauses them together. If only one is playing, select it and tap PLAY. **Do not use FUNCTION + PLAY in that case: it would start both.**
6. Keep the player powered on while it saves. Muting both songs is not enough; both transports must be paused. Wait for the normal deck lights to return before powering off, unplugging or entering bootloader mode.
7. Browse to the new song or find it in **On your player** on the site. Play it using Track 1, unmuted, with its fader raised. Export it later from the site if you want a WAV file.

The take is one stereo mix on **Track 1**. Tracks 2-4 are empty; recording does not split the finished mix back into eight stems. It may replay more quietly through the player's normal mix levels. Export preserves the recorded level.

### Limits to remember

Historical 0.4.4 evidence is one non-silent 3.17-second take saved to Song 5 and exported. It does not verify a long performance, all-eight-stem recording, or recording while USB audio and mirroring are active. Check the hardware report for subsequent recording tests on 0.4.5.

A take lasts up to about eight minutes. At the limit it stops automatically, then follows the same pause-and-save process. The take is not ready until saving succeeds.

Flashing outer side lights indicate failure. Check the site's library before treating a take as saved. If storage cannot keep up or verification fails, the failed take is not published as a playable song.

Keep power connected during saving. Switching off or losing power during a storage write can lose the take or damage its library entry. Existing songs are not intentionally overwritten.

## 06 / Use the companion site

Open [sp-1.xyz](https://sp-1.xyz/). The site is a browser instrument and a companion to the physical player. You can prepare music without hardware, or connect the SP-1 to follow its controls, lights and playback.

### Make stems in the browser

1. Choose **Drop a song here** and select a WAV, MP3, M4A or FLAC file.
2. Choose **Separate into 4 stems**. Keep the tab open while it works. The first run downloads a large separation model; processing can take several minutes.
3. Use the browser's play button, faders, Mute and Solo to explore the song. Speed / pitch changes both together.
4. Choose **Export stems** to save the prepared stems as a ZIP of WAV files.

Already have stems? Choose **Have stems?** and assign your files to Vocals, Drums, Bass and Other. Use a common starting point and keep any leading silence so they stay aligned. Missing stems stay silent. The studio demo is another quick way to try the browser mixer.

### Mix two browser songs

Choose Deck A or Deck B before importing a song. The four stem controls apply to the selected deck. Use each deck's song selector to load any song kept in this tab, its Play button to start or pause it, and its Deck level to balance it against the other song. Cue returns that deck to the beginning. Changing one deck leaves the other playing.

**Try both demos** loads two generated songs for practice. **Play both** starts both; **Pause both** stops both. Remove an unused song from the session to free memory. The session library is temporary and disappears when the page is reloaded, so export work you want to keep.

On a keyboard, A and B select a deck, Space plays or pauses it, Shift + Space controls both, and keys 1-4 mute its stems. Shortcuts do not override typing in a field and are inactive while the physical player is connected.

Separation runs on your device. Audio is not uploaded to a server. Browser files must be under 200 MB each and at most ten minutes; songs sent to the physical player have the shorter limit on the next page. Export stems saves the prepared source stems, not a recording of your browser mix.

The file-size limit bounds decoding and separation memory: a compressed song expands into audio samples, four output stems and processing buffers. The browser mixer also limits retained decoded audio to 768 MiB. That budget does not include every temporary allocation or the separation model. Use shorter files and remove unused session songs if memory is exhausted.

### Match tempo, key and individual stem pitch

1. Load songs on both browser decks and open **Match songs**. Choose the master deck whose tempo and key you want to keep.
2. Choose **Analyze both songs**. Check the BPM and first-beat positions. Use Half or Double if the estimate follows the wrong pulse, or enter a corrected value. Key can remain Unknown; do not treat an estimate as certain.
3. Keep **Match the other deck's tempo to the master** enabled to prepare the follower at the master's tempo. Enable key matching only after checking both keys. It shifts vocals, bass and other; drums retain their own pitch setting.
4. Set each follower stem's semitone offset. For example, +2 raises only its chosen stem by two semitones while preserving the prepared song's duration. These offsets add to any key-matching shift.
5. Choose **Prepare Deck A** or **Prepare Deck B**. The finished song is selected for export/upload, and its original remains in the tab's library. Cancel leaves the existing songs intact.
6. Choose **Align on next beat** to start or align the two grids on the browser audio clock. Listen and correct the grids if needed. Export the selected prepared stems, or connect the player and upload them to an empty slot.

This prepares new audio files in the browser. Uploading them does not transfer the browser beat grids: teach the physical player with FUNCTION + Track 1 before using its live sync. Browser key/pitch preparation is separate from on-device beat following. A fixed beat grid cannot follow changing tempo. Pitch processing can soften transients or change vocal tone; it does not preserve formants or turn a major song into a minor arrangement. The disconnected browser mixer's normal Speed / pitch control still changes timing and pitch together.

### Connect the player

1. Use Chrome or Edge on a computer for USB features. Connect with a USB data cable and power on normally. Close any other app using the player's connection.
2. Choose **Connect SP-1**, keep **Bonsai 8 / Dual Deck** selected, then **Connect player**. Select Bonsai 8 in the chooser. Older compatible firmware may use the name SP-1 Dual Deck.
3. Use the physical controls. The digital model follows reported fader positions, button presses and lights. The connected site shows the selected deck's settings and both song positions.
4. Choose **Listen here** to hear the physical stereo mix through the computer and see its waveform. Allow audio-input permission; the site uses the player's named USB input.

The connected 3D model is a monitor. Its browser controls do not remotely move physical faders or change physical tempo. **Load A / Load B** in the library are separate actions that can choose the songs on the player.

USB audio is the combined mix, not eight separate input channels. The physical master changes its level. Use computer output volume for local listening. Listening through both outputs can sound like an echo because USB audio and the screen add delay. The song timers show source position, not beat markers.

## 07 / Manage songs

Connect Bonsai 8, then use **On your player**. It lists occupied song slots and their lengths. A successful site upload keeps its source title for that connection. Current firmware does not store original filenames or a durable content identity, so reconnecting can show Song 1, Song 2 and so on. The site does not guess names from reused slots.

### Choose any song for either deck

Use **Load A** or **Load B** beside a song. That deck stops and cues the chosen song; the other deck keeps its playback state. Select the target deck on the physical player and tap PLAY when you want it to enter. Refresh reloads the library list.

### Upload prepared stems

1. Separate a song in the browser, or import existing stems with **Have stems?**
2. Connect the player and choose an empty destination under **Save prepared stems to**.
3. Choose **Upload these stems**. Keep USB connected and the tab open until the site confirms that upload and verification finished.
4. Use Load A or Load B to cue the new song, then press PLAY on that deck.

The player has **16 song slots**, with up to four stems in each. Uploads are limited to about eight minutes. Use matching starting points; shorter stems are padded with silence. The player itself does not separate a mixed song into instruments.

Upload sends the selected prepared stems, including completed Match songs tempo and per-stem pitch processing. It does not include temporary browser fader, mute, solo, deck-level or tape-speed settings. Keep a copy of the original stems on your computer.

### Export or delete

Choose **Export** beside a song to download it. A single-stem song, including a recorded mix, exports as a WAV. A multistem song exports as a ZIP of WAVs.

Choose **Delete** beside the song you want to remove and check the slot in the confirmation. Export it first if you want a copy. Deletion frees that slot for a new upload; it does not securely erase the old audio data.

**Uploading, exporting and deleting pause both decks.** These operations stop USB listening too. Wait for completion, then restart physical playback and choose Listen here again if needed.

Library actions are blocked while recording, draining or waiting to save. Stop recording and pause both decks first. Do not disconnect during a transfer or start a second uploader. A firmware update does not require you to upload your existing songs again.

## 08 / Install or update Bonsai 8

The website's **Install Bonsai 8** button updates the application on the player. It does not intentionally erase or replace the music library. Read the version and release status shown in the installer before continuing.

1. Finish any recording or file transfer. Disconnect the site from the player and close other player connections.
2. Keep USB connected. On the powered-on SP-1, hold **Track 1 + Track 4 for three seconds** to enter bootloader mode. Playback stops.
3. Open **Install Bonsai 8** on [sp-1.xyz](https://sp-1.xyz/). Wait for the published firmware check to finish.
4. Select the installation checkbox, then choose **Choose bootloader & install**. Select the SP-1 bootloader in the browser's device chooser.
5. Keep USB connected and the tab open until the installer reports completion. Do not start a library transfer during the update.
6. Hold FUNCTION for about 1.5 seconds to power on if needed. Reconnect normally, check the reported version and song list, and try playback and the controls.

The installer checks the downloaded image before sending it and checks the bootloader's responses. Successful transfer still needs a normal power-on and playback check afterward.

### Which mode should I use?

**Normal power-on** is for music playback, live mirroring, USB listening and managing songs. **Bootloader mode** is only for installing firmware. The live controls and audio are not available there.

If the chooser finds no bootloader, close it, enter bootloader mode again and retry. If USB connection options are unavailable, open the site in Chrome or Edge on a computer. A cable that only charges cannot transfer firmware or audio.

### About Bonsai 8

Bonsai 8 is custom community firmware for the SP-1, based on the SP-1 Tape Looper project. It is not an official Teenage Engineering release. The companion site includes source and license credits in **About this project**.

For illustrated, step-by-step help, open [sp-1.xyz/guide.html](https://sp-1.xyz/guide.html).

## 09 / If something feels wrong

| What you notice | Try this |
| --- | --- |
| Deck B plays silently | B starts with all levels at zero. Select B, move its faders to minimum for pickup, raise the wanted stems, unmute them and check master. |
| A fader does nothing | Cross its stored level to pick it up. Switching decks can require pickup again. |
| PLAY restarts the song | The press lasted long enough to cue. Use a short tap for pause/resume. |
| FUNCTION powers off | Hold it alone for less than 1.5 seconds when switching decks; press the second control promptly for combinations. |
| Solo still leaves another song audible | Solo affects only the selected deck. Pause or mute the other deck separately. |
| Songs drift or clash | Tap each playing song's quarter beats with FUNCTION + Track 1, then sync the selected follower with FUNCTION + Track 2. Use FUNCTION + Track 3 + wheel for phase adjustment. Keys and musical phrases are not matched automatically. |
| Recording stopped but no new song appears | Pause both decks and wait for normal side lights. Outer flashing lights mean the take failed. Check for an empty slot before trying again. |
| A recorded mix only uses Track 1 | Correct: the complete stereo mix is on Track 1. The other three stems are empty. |
| The site cannot connect | Power on normally, use a data cable and desktop Chrome or Edge, and close other player connections. |
| No sound through Listen here | Check audio-input permission and whether the computer sees a Bonsai 8 or SP-1 Dual Deck USB input. Reconnect the powered-on player. |
| Music becomes choppy | Stop Listen here and disconnect the site to compare standalone playback. Historical 0.4.4 tests failed combined USB audio and mirroring even with its filter bypassed. Check the hardware report for the current version and results. A moving timer does not prove clean audio. |
| Side lights alternate in pairs | Storage is unavailable or unrecognized. Reconnect and check the site status; do not erase an existing library as a troubleshooting step. |

For a repeatable problem, note the firmware version, songs on A and B, active stems, speeds, and whether recording or USB listening was active. Keep your source audio backed up.
