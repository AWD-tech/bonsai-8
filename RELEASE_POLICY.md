# Device-tested releases

The owner requires every successful flashed and device-tested build to reach the Git repository, its pull request and the website installer. This is a standing project requirement.

1. Finish implementation, run host regressions and build the application. Assign a new version whenever executable code changes. Package committed source and record the binary hash, application address and resource use.
2. Prepare the updater before asking for bootloader mode. Flash only the application region. Never initialize, format, delete or re-upload songs as part of a firmware update.
3. After power-on, confirm the runtime version and compare the occupied library slots, stem presence and exact lengths with the pre-update listing. Listing preservation does not prove every audio sector unchanged. The updater's CRC acknowledgements are not binary readback.
4. Measure a stable twenty-second window with both decks playing and all eight stems raised/unmuted. Repeat dry, full filter, full echo, full reverb, and USB capture with mirroring. Read counter changes from the raw intermediate samples, rather than accepting a printed PASS. A stem may vary by one ADC gain level at its full endpoint; every sample must remain at least250/256 and larger changes invalidate the window. Record those ranges explicitly. Test changed functionality separately: song browsing during playback, recording/save/export, pitch, sync or storage operations need their own real-device checks.
5. Save private raw evidence locally. Update `HARDWARE_TEST.md`, the pull request and release notes with the exact workloads, failures, limits and outstanding gates. Never commit music, raw device serial numbers or credentials.
6. Run `tools/release.py --help`. Supply the pinned manifest/binary, flash log, before/after library listings and all five passing playback reports. Its `--stage site/public/firmware` option stages an immutable versioned binary, release summary and current installer manifest. It does not flash, publish or push.
7. Copy the staged release to the website working directory, run website checks, deploy, and verify the live installer manifest and binary hash. Update the repository and PR to the same release. A new version is delivered only when both destinations match.

If a relevant hardware test fails, retain the previous verified installer version and label the new binary a candidate. Publish a candid progress report; do not promote a failing or partially tested build. A passing steady-window suite does not establish every possible effect, speed, recording or USB combination. Keep those limits in the release notes.

Unknown stock-format libraries remain preserved and write-locked. Preservation does not mean the custom player can interpret the stock song format. Library initialization is a separate destructive action and is never part of website flashing.
