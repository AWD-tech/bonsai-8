# Contributing

Development repository: https://github.com/AWD-tech/sp1-dual-deck (private).

Keep `main` as the reviewed baseline. Create a branch for each change and open a pull request describing its behavior, validation and remaining hardware limits. An ordinary code PR should not flash a connected device or initialize its library as part of automated testing.

Read `USER_MANUAL.md` for current user controls and `HARDWARE_TEST.md` for observations. The source contains the installed 0.3.1 hardware-test candidate. Runtime version, library preservation and macOS USB audio enumeration are verified. The website now connects to the SP-1 audio input and displays its live waveform. Audible quality and endurance still need testing; playback-buffer underruns have been observed.

Run `tools/test.sh` for the shared C engine, telemetry/capture sanitizers and Python transfer tests. Install pinned dependencies with `python3.11 tools/setup.py`; build with `tools/build.sh` and the Zephyr SDK identified in README.md. Automated host tests do not establish audio quality, USB compatibility or flash safety.

For manual changes, update `USER_MANUAL.md` and run `python tools/render_manual.py` with ReportLab installed. Check the generated PDF visually before committing it.

Preserve upstream MIT notices. `README-upstream.md`, the older Tape Looper documentation and `sp1_looper.bin` describe upstream, not this custom firmware. The baseline derives from upstream commit 44ba1ecbec6c844dba7f47eacee94c53af8ab10d.

Do not commit build environments, private music, prepared-library contents, raw USB captures, device serial numbers, here.now credentials or local account tokens. Raw hardware evidence stays local; summarize relevant measured outcomes without overstating what was tested.
