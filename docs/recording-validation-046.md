# Recording host validation for 0.4.6

Checked 2026-10-17 on `feat/bpm-playlists`. Six new recording integration tests,
the existing recording core tests, 16 actual-control replay tests and 16 site
library tests passed. This is **host execution with synthetic audio and simulated
storage**, not a physical SP-1 recording, reboot, USB transfer or endurance pass.
The 0.4.6 candidate remains unchanged and is not promoted by these results.

The installer binary still hashes to
`8a421d100e4b91a9b1ceefa051d09e28e94951037cdd6c16d1c37b4dae160a32`.
These tests compile the source into host test executables; they do not execute,
rebuild or replace that firmware binary. No serial ports or private music were
used. Temporary audio and test executables stay outside Git.

## Reproduce

Run the full existing host suite, including the new adapter tests and a paced
one-hour concurrent core replay:

```sh
BONSAI_RECORD_SPSC_FRAMES=86400003 tools/test.sh
```

Or run the focused adapter, control and site library checks:

```sh
.build-env/python/bin/python -m unittest discover -s tests -p 'test_record_adapter.py' -v
.build-env/python/bin/python -m unittest discover -s tests -p 'test_controls.py' -v
node --test site/tests/device-library.test.js
```

The adapter fixture uses `cc` with AddressSanitizer, UndefinedBehaviorSanitizer,
and warnings treated as errors. Node runs the actual site export module.
The six focused adapter cases completed in 10.026 seconds on this host; this
elapsed time is not a microcontroller performance result.

## Actual paths exercised

`tests/test_record_adapter.py` extracts the production `dual_audio_block`,
`dual_record_service`, `dual_record_publish`, `dual_write_checked`,
`dual_read_index`, `dual_song_present` and checksum/failure functions from
`firmware/src/dual_firmware.inc`. It extracts metadata structures and helpers
from `main.c`, then links the actual recording, library, playlist, dual engine
and stretch C modules. The extraction fails if a required function is absent.

`tests/helpers/record_adapter.c` supplies scheduling, USB, cache and eMMC
boundaries. Eight synthetic stereo stems feed the real mixer. The actual audio
callback sends its final 48 kHz stereo output to the actual recorder. An
independent oracle averages adjacent frames toward zero and floors each result
to a multiple of four. Every recorded frame is checked against that oracle;
every unused frame in the final sector must be zero.

Synthetic storage and expected PCM use temporary files with fixed-size buffers.
After save, the in-memory metadata is cleared and the actual index reader loads
the persisted bytes. This models a metadata reload, not loss of power, a real
reboot or eMMC durability. The engine/recorder is serviced deterministically;
the harness does not reproduce Zephyr priorities or competing eMMC playback
reads. USB servicing is a call-boundary stub.

## Passed coverage

| Check | Host result |
| --- | --- |
| Maximum supported take with eight real mixer voices | 23,045,120 accepted 48 kHz stereo frames, 11,522,560 recorded 24 kHz frames, 480.106667 simulated seconds, 82,304 P14S sectors. Automatic stop drained exactly; no engine underruns in this scheduled replay. |
| Maximum take sample and export integrity | All 11,522,560 stereo frames matched the independent oracle after C decoding and again after the actual site's `DeviceLibrary.export`. The WAV contains 46,090,284 bytes including its header; padding does not extend its exact duration. |
| Recorder memory | `sizeof(struct bonsai_record)` is 32,808 bytes. Its fixed ring holds 8,192 native frames. Maximum-take peak occupancy was 264 frames under this ideal service schedule. No duration-sized recorder allocation was introduced. Site export necessarily allocates a duration-sized PCM/WAV buffer on the host. |
| Concurrent recording core endurance | 86,400,003 native frames, 3,600.000125 simulated seconds, all decoded samples checked, exact partial tail and committed counters. Producer and consumer run on separate pthreads; the test producer explicitly waits for room. This proves sample/state integrity under paced concurrency, not nonblocking physical storage throughput. |
| Save boundary | Stop is acknowledged by the real audio producer. Neither one paused deck nor two paused decks with remaining fade envelopes can start flush or metadata publication. Saving begins only after both envelopes reach zero. |
| Every slot | Save and reload passed for all 16 possible empty target slots, including metadata-sector boundaries. Other slot records, extended rows, playlist blocks and the extended-table tail remain unchanged. Only the mixed recording's first stem is visible. |
| Failure guards before publication | Ring overflow after an earlier successful sector, failure of the second audio write, and audio corruption detected at post-flush hash verification all fail the take without metadata publication. Failed audio writes do not advance committed sector ownership. |
| Save IO failures | Each of the 20 IO operations in a 12-sector save was independently failed. All failures report runtime state 5 and error 5. The first 18 leave the target unpublished; the final two expose the limitation below. |
| Admission/restart | Unknown storage, invalid extended metadata, active transfer mode, insufficient capacity and a full library reject recording. Empty takes cannot publish. Failed takes can restart; successful takes do not overwrite the preceding recording. |
| Small complete cross-language export | Actual C recorder/save output for slot 12, 1,664 exact stereo frames in 12 sectors, passes the site's real transfer framing, metadata validation, P14S decoder and WAV writer. Every WAV sample matches; export issues only handshake, reads and exit, with no storage writes. |
| Existing control regressions | All 16 replay tests pass, including full-release rearming for the recording chord and browsing exclusion while recording. ADC readings and elapsed time are synthetic; these are not physical-button tests. |

The core suite also retains exhaustive signed PCM16 quantization, odd input
chunks, an unpaired final frame, golden P14S bytes, integer-counter wrap,
commit ownership, empty stop and overflow checks.

The companion's native browser test can consume the same short persisted
fixture; its browser evidence and limitations are reported separately in
[browser-audio-validation-046.md](browser-audio-validation-046.md).

## Confirmed final-publication limitation

The format has no transaction journal. `dual_record_publish` persists the
prepared metadata with presence clear, verifies it, then writes the one final
sector containing `present[0] = 1`. If that write succeeds but its subsequent
cache flush or readback fails, the runtime reports a failed save while a reload
can already see the song as present. The new regression explicitly asserts this
visible-failed state for both cases; it does not label them unpublished.

The injected model leaves successful writes intact. A real cache failure,
partially completed write or power loss can have a different outcome, including
an unknown or damaged final sector. Durable atomic publication and rollback are
not established. Changing this behavior requires a separately versioned firmware
change and later device testing; the existing 0.4.6 binary was preserved.

## Remaining physical checks

Sustained recording while actual eMMC playback reads, USB audio and mirroring
compete; callback deadlines; audible dropouts; physical shortcut handling;
pause/save latency; real cache errors; unplug/power-loss behavior; reboot
durability; and complete transfer/export from the physical player remain
unverified. The paced one-hour core replay exceeds one supported take only to
stress the core's ring and counters; the actual adapter's eight-minute limit was
tested separately and remains enforced.
