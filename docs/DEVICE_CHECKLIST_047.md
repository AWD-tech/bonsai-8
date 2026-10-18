# Bonsai 8 0.4.7 device checks

The candidate needs these physical checks before release promotion. Host audio
screens do not measure the SP-1's CPU deadlines, eMMC timing or audible output.
No new device results are implied by this checklist.

## Install and preserve the library

- Capture the current firmware, song index, playlist banks and representative
  audio-sector hashes. Disconnect every website/serial owner before updating.
- Flash only the application using the pinned 0.4.7 binary and manifest; never
  initialize the library. Verify the normal runtime reports `bonsai-8-0.4.7`.
- Compare the index, playlists and audio hashes with the preflash snapshot.

## Tempo, beat sync and sound

- Start both decks, unmute every stem and raise all eight saved stem levels.
  Establish a dry 1x baseline with the site disconnected.
- Use the wheel alone on the selected deck to change tempo. Check small changes
  around 1x and the 0.5x/1.25x endpoints on bass, vocals and percussion. Listen for
  pitch drift, repeated/skipped attacks, clicks, stereo movement and dropouts.
- On each playing deck, hold FUNCTION and tap Track 1 on at least four steady
  quarter beats. Select the follower, then hold FUNCTION and tap Track 2.
  Verify that the audible beats settle together, not just the displayed clocks.
- Hold FUNCTION + Track 3 and tap the wheel in both directions. Check phase
  adjustment and recovery; change the master tempo and check following again.
- Browse/load a different song on one deck with FUNCTION + wheel while the
  other continues. Teach a fresh grid for the replacement song before syncing.

## USB audio, controls and endurance

- Connect the current site, start Listen here and verify non-silent stereo
  through the computer as well as physical output. Test synced playback with
  eight stems, physical master +/−, fader moves and button/LED mirroring.
- Capture diagnostics for stable dry and USB-plus-mirroring windows, including
  all eight gains, applied tempos, underruns, clips, storage errors and callback
  time. Require zero new dropouts/errors; a host simulation cannot clear this.
- Disconnect/reconnect monitoring, then mix for 20–30 minutes with tempo changes
  and independent song selection. Check that neither deck stalls or drifts.

## Recording while mixing

- With an empty slot, record several minutes of the synced A+B mix using
  FUNCTION + both volume buttons held for one second. Release all three before
  repeating the gesture to stop. Include volume/tempo moves.
- Pause both decks and allow saving to finish. Restart and play the saved take.
  Export it through the site and check full duration, stereo, sound and no gaps.
- Check the library if save reports failure: the existing final metadata
  acknowledgement ambiguity can leave a visible take. Do not assume it is safe
  to retry blindly.

Record evidence against the exact binary SHA-256, following
[the release evidence contract](RELEASE_EVIDENCE.md). Only after these gates pass
should the repository release and public website installer be promoted.
