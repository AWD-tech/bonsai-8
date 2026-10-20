# Unattended 0.4.6 validation — October 17, 2026

**Promotion blocked. The firmware remains a candidate.** No physical SP-1 was
connected. All work below used the unchanged production source, simulated
storage and/or an isolated browser. The existing binary was neither rebuilt nor
reflashed, and no user song storage was accessed.

| Area | Result | Evidence and limits |
| --- | --- | --- |
| Recording/save/export | Host pass with publication limitation | The actual adapter recorded the maximum 480.106667 simulated seconds. All 11,522,560 stereo frames survived actual site WAV export exactly. A paced one-hour concurrent core replay passed. Physical throughput, cache durability and restart remain untested. |
| Recording fault handling | Known final-acknowledgement ambiguity | All 20 save IO failures were injected. The first 18 leave the take unpublished. Failure of the final flush/readback after the presence write may leave a visible take while reporting failure. Check the library before retrying or trusting it. |
| Live sync logic/endurance | Host pass | 245 BPM/rate combinations, tapped-grid following, master changes/nudges and 600 simulated seconds with eight stems passed. Generous host scheduling does not establish MCU deadlines. |
| Rendered live-sync audio | **Fail** | Seven pitch/spectral screens failed. A 20 Hz tone became 22.051 Hz at 1.125x. A 120/150 BPM pulse test produced 71 follower onsets instead of 60, despite nominal lock. Musical listening and hardware testing remain separate gates. |
| Website audio | Native-browser synthetic pass | Nine monitor cases, a 60.139-second stereo run, three offline mixer cases and native decoding of a host-recorded exported take passed. The browser was muted; no physical USB input, permissions, speakers or headphones were tested. |
| Release tooling | Guard strengthened | Steady playback alone could previously emit `verified`. Version 0.4.6 onward now also requires exact-binary physical feature reports with checked artifact hashes before staging. Software fixtures cannot satisfy the physical environment requirement. |

Detailed, reproducible reports:

- [Recording adapter, export and failure injection](recording-validation-046.md)
- [Sync measurements and failing audio screens](sync-validation-046.md)
- [Real browser graph with synthetic input](browser-audio-validation-046.md)
- [Physical feature evidence contract](RELEASE_EVIDENCE.md)

GitHub CI runs the functional suite, the separate live-sync audio quality gate,
and the native browser fixture. The quality gate is intentionally **not** marked
continue-on-error: the measured failure must remain visible until the DSP is
corrected. A green functional suite does not mean the audio quality gate passed.

Existing October 17 physical playlist and steady playback results remain valid
within their documented scope in [HARDWARE_TEST.md](../HARDWARE_TEST.md). They do
not substitute for sustained recording, live-sync quality, browser speaker
output or browser flashing checks. Future firmware fixes require a new version
and new physical tests; the published 0.4.6 binary stays immutable.
