# Physical deck playlists — 0.4.6 candidate

Each deck has its own saved selection of songs, ordered from lowest to highest
source BPM. A song can belong to A, B, both or neither. Equal BPMs use song-slot
order; songs with unknown BPM come last. An empty playlist stays empty.

## Set up on the site

Connect firmware 0.4.6 or later, then open **On your player → Deck playlists ·
BPM order**. Enter each song's original BPM and tick A and/or B. Leave BPM blank
when unknown. The two lists preview the physical browsing order; clicking a song
loads it on that deck. **Save playlists to player** pauses both decks and writes,
flushes and reads back the settings. The settings survive disconnect and restart.
Editing the preview alone does not change the physical player's browsing order.

## Mix and replace songs

1. Tap FUNCTION to select A or B.
2. Hold FUNCTION and tap the rocker up/down to load the next/previous song in
   that deck's playlist. Repeated taps continue through the list and wrap.
3. The selected deck stops and cues the replacement at its start. The other deck
   retains its song, playback state and stem levels. Tap PLAY to start the new song.
4. Balance its stems, then switch decks whenever you want to replace the other song.

Lower or mute the deck you intend to replace first for a smoother transition.
This is a two-deck workflow: loading a replacement cannot keep the old song on
that same deck playing. Song changes remain blocked during recording/saving.

Without saved playlists, both decks include the whole library. At boot with
saved playlists, each deck cues its first song, paused; an empty deck playlist
loads nothing. Existing song-number lights still identify the physical slot,
not its position within the playlist. Explicit site **Load A / Load B** buttons
can load any occupied slot, even outside the saved playlist.

BPM ordering does **not** establish beat position, align phrases or match keys.
Playlist BPM is a browsing label; the live sync engine still uses freshly tapped
session grids. Changing a song resets that deck's live grid. Names currently last
for the site connection; physical browsing uses slots and LEDs, not a text screen.

## Storage contract

Supported layout: SE3A, 512-byte sectors, 16 songs, four fixed stem regions per
song, audio starting at sector 4096 with 86016 sectors per stem. The upstream
layout defines sectors 1–4095 as spare apart from its explicit index (0–1), grid
(2) and X3 (3–5) structures. Bonsai reserves **6 and 7** as its versioned playlist
extension. This is not a claim about unknown stock formats: the existing layout,
index and X3 validators must pass before the site writes anything.

Each sector contains `B8PL`, little-endian version 1, uint32 generation, a zero
reserved word, then sixteen eight-byte rows. A row holds BPM × 100 in bits 0–15,
A/B membership in bits 16/17, and a uint32 descriptor CRC. Bytes 144–507 are zero;
508–511 hold the IEEE CRC32 of bytes 0–507. BPM is zero or 2000–40000 inclusive.
Generations compare modulo 2^32; a newer bank wins, with bank 0 winning a tie.

Save replaces only the inactive bank and preserves the last valid bank. It
rejects nonblank foreign data and unknown schema versions. A failed or torn bank
is ignored in favor of the valid bank; if neither validates, browsing uses both
full-library defaults. Writes run through the existing paused, exclusive transfer
owner; the audio callback never performs playlist IO. Browsing uses at most 16
cached rows and does not write storage.

The descriptor CRC covers this slot's 44 index bytes, 16 content-count bytes and
64 X3 bytes. Changed descriptors reset that song to unknown BPM/included on both
decks. This is **not an audio-content identity**: replacing audio through another
firmware/tool with identical descriptors can retain old labels. Recheck playlists
after using another uploader. The site explicitly clears a deleted slot's playlist
row before deleting its index, so an identical replacement uploaded here does not
inherit its previous settings. A failed deletion can therefore leave that song's
playlist settings reset, while its audio remains present.

No playlist save touches song audio, index, grid or X3 sectors. Application-only
firmware flashing does not write these eMMC banks. The current hardware-test
candidate passed real-device save and restart persistence checks on October 17,
2026. Browsing under load and playback checks remain required before promotion
to a verified release.
