/* SPDX-License-Identifier: MIT */
#ifndef BONSAI_LIBRARY_H
#define BONSAI_LIBRARY_H
#include <stdbool.h>
#include <stdint.h>

#define BONSAI_LIBRARY_META_BYTES 1024u
#define BONSAI_LIBRARY_X3_BYTES 1536u
#define BONSAI_LIBRARY_SLOTS 16u
#define BONSAI_LIBRARY_SECTOR_FRAMES 140u
#define BONSAI_LIBRARY_MAX_NATIVE_FRAMES (35840u * 643u / 2u)
#define BONSAI_LIBRARY_TRACK_SECTORS 86016u

/* Prepare an EMPTY slot for one stereo P14S recording at 24 kHz, on stem 1.
 * Both buffers must contain the complete current on-device metadata images.
 * Checks the existing index signature, slot selection and X3 version/checksum;
 * never repairs/reinitializes invalid metadata or replaces an occupied slot.
 * sectors must equal ceil(native_frames / 140), within the supported length.
 * Success leaves all four presence flags zero: this stage is NOT playable.
 * All unrelated bytes, including reserved headers/settings/tails, survive.
 * Any false result leaves both buffers byte-for-byte unchanged.
 * These helpers perform no storage IO and require exclusive buffer ownership. */
bool bonsai_library_prepare(uint8_t meta[BONSAI_LIBRARY_META_BYTES],
                            uint8_t x3[BONSAI_LIBRARY_X3_BYTES],uint32_t slot,
                            uint32_t native_frames,uint32_t sectors);

/* Call only after the audio and prepared X3/index have been written, flushed
 * and verified. Validates the unpublished single-stem layout, then changes
 * ONLY this slot's stem-1 presence byte to 1. The caller still must persist
 * and verify that final index write. The helper cannot verify stored audio.
 * Returns false without mutation if the slot is occupied or not prepared. */
bool bonsai_library_publish(uint8_t meta[BONSAI_LIBRARY_META_BYTES],
                            const uint8_t x3[BONSAI_LIBRARY_X3_BYTES],uint32_t slot);
#endif
