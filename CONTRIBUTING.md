# Contributing

Development repository: https://github.com/AWD-tech/bonsai-8 (private).

Keep `main` as the reviewed baseline. Create a branch for each change and open a pull request describing its behavior, validation and remaining hardware limits. An ordinary code PR should not flash a connected device or initialize its library as part of automated testing.

Read `USER_MANUAL.md` for controls and `HARDWARE_TEST.md` for observations. Bonsai 8 0.4.1 was flashed and its runtime version confirmed on October 3. Stable twenty-second dry and two-filter tests passed with all eight stems raised, no mutes and no new dropouts. Echo, reverb, USB capture/mirroring and changed-feature checks remain separate gates. Do not label an unflashed source/package change as installed or verified.

Follow `RELEASE_POLICY.md`: each successfully flashed and device-tested release must update the Git repository/PR and the website's flashable version. Keep the last verified installer when a new candidate fails. `tools/release.py` checks raw measured evidence and stages the pinned application; it cannot establish missing hardware checks.

Run `tools/test.sh` for the shared C engine, telemetry/capture sanitizers and Python transfer tests. Install pinned dependencies with `python3.11 tools/setup.py`; build with `tools/build.sh` and the Zephyr SDK identified in README.md. Automated host tests do not establish audio quality, USB compatibility or flash safety.

For manual changes, update `USER_MANUAL.md` and run `python tools/render_manual.py` with ReportLab installed. The renderer writes `output/pdf/Bonsai-8-User-Manual.pdf`. Check every generated page visually before committing it. The previous SP-1 Dual Deck PDF is historical; the reviewed Bonsai 8 PDF is the candidate manual.

Preserve upstream MIT notices. `README-upstream.md`, the older Tape Looper documentation and `sp1_looper.bin` describe upstream, not this custom firmware. The baseline derives from upstream commit 44ba1ecbec6c844dba7f47eacee94c53af8ab10d.

Do not commit build environments, private music, prepared-library contents, raw USB captures, device serial numbers, here.now credentials or local account tokens. Raw hardware evidence stays local; summarize relevant measured outcomes without overstating what was tested.

Keep control documentation explicit: FUNCTION plus rocker taps or a held rocker browses all occupied slots in either direction on the selected deck. Per-stem level control and combined master volume are implemented; pitch follows deck speed. No automatic beat/key sync or independent pitch/time stretching should be implied before implementation and validation. SFX are one filter/echo/reverb per stem, edited while holding FUNCTION + that track; settings remain after release and reset after a power cycle. Standalone recording uses FUNCTION + both volume buttons held one second; after stopping, both decks must pause for verified publication. Browser-only recording does not satisfy offline recording. Full eight-stem playback, SFX, recording, save/recovery and USB together require hardware testing.
