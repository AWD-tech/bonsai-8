/* SPDX-License-Identifier: MIT */
#ifndef BONSAI_PLAYLIST_H
#define BONSAI_PLAYLIST_H
#include <stdbool.h>
#include <stdint.h>

/* Bonsai's versioned extension to the supported SE3A layout. Index: 0..1,
 * grids: 2, X3: 3..5, playlists: 6..7, fixed audio starts at 4096.
 * Two alternating banks; never write an unrecognized nonblank bank. */
#define BONSAI_PLAYLIST_BLOCK 6u
#define BONSAI_PLAYLIST_BYTES 512u
#define BONSAI_PLAYLIST_SLOTS 16u
#define BONSAI_PLAYLIST_DEFAULT (3u << 16)
struct bonsai_playlist {
 uint32_t generation;
 uint32_t row[16]; /* BPM in hundredths, membership A/B in bits 16/17. */
 uint32_t identity[16]; /* Descriptor fingerprint, NOT an audio-content hash. */
};
bool bonsai_playlist_decode(const uint8_t bank[512],struct bonsai_playlist *out);
/* -1: neither valid. Generation comparison tolerates uint32 wrap. */
int bonsai_playlist_bank(const uint8_t banks[1024]);
uint32_t bonsai_playlist_identity(const uint8_t meta[1024],const uint8_t x3[1040],unsigned slot);
/* Occupied slots only; unknown BPM last, equal BPM in physical slot order.
 * Returns -1 for an empty playlist. Current song need not be a member. */
int bonsai_playlist_next(const uint32_t rows[16],uint16_t present,unsigned deck,int current,int direction);
#endif
