# Device upload responsiveness — 20 October 2026

The regression runner calls the real `prepareStems` and the real
`DeviceLibrary.upload` checksum stage. Its transport stops at the first
`withTransfer` boundary; it does not request or open a physical serial port.

```sh
PLAYWRIGHT_MODULE=/absolute/path/to/playwright/index.mjs \
PLAYWRIGHT_CHANNEL=chrome \
node site/tests/upload-responsiveness-browser.mjs
```

Optional `UPLOAD_TEST_SECONDS=480` exercises four eight-minute stems;
`UPLOAD_VALIDATION_REPORT=/absolute/path/report.json` saves the measurements.
The runner starts and removes an isolated Chrome profile. It connects with
`noDefaults: true`, because Playwright's ordinary focus emulation would make a
background tab falsely report itself as visible. No existing browser tabs are
used.

## Reproduced cause

Before the correction, four two-minute stereo stems at 4x CPU throttling caused
main-thread tasks of **58, 157, 174, 161, 188 and 219 ms**. Preparing the sectors
took 788 ms. The following 220 ms checksum step performed two whole-library
concatenations before the native digest. Those copies alone added about 80 MiB
for this 40 MiB encoded song, before the digest's internal buffer.

The preparation loop packed each complete stem synchronously and only yielded
after the stem. A single longer stem therefore blocked browser input and
painting for longer. The old final yield also used a foreground-sensitive timer.
This establishes the long-task problem; it does not prove the precise browser
policy that affected the reported user's original tab.

## Correction

- Keep the existing native 24 kHz stereo resampling and exact P14S packing.
- Encode bounded 256-sector batches in a worker; use MessageChannel task yields
  for the equivalent fallback when workers are unavailable or blocked by CSP.
- Calculate the exact existing SHA-256 identity incrementally in the worker,
  using bounded 128 KiB transfers. Frame prefixes and resume checkpoint keys are
  unchanged. No combined whole-library buffer is allocated.
- Report progress within a stem and during checksum preparation.
- Accept an optional abort signal during preparation; worker requests reject and
  terminate cleanly. Cancellation during native offline rendering takes effect
  when that render resolves; it does not claim to stop a USB transfer.
- Expose a conservative preparation-memory estimate for the caller's existing
  retained-audio budget: all encoded stems + largest stereo render + 4 MiB
  staging. This protects preparation allocations as well as file imports.

## Browser results

Chrome 155.0.8059.80, 4x CPU throttling, four two-minute stereo stems plus a
partial final sector:

| Mode | Preparation | Checksum | Main-thread tasks over 50 ms |
| --- | ---: | ---: | ---: |
| Foreground worker | 368 ms | 1,818 ms | 0 |
| Actually hidden tab, worker | 1,467 ms | 4,778 ms | 0 |
| Actually hidden tab, fallback (four ten-second stems) | 293 ms | 1,678 ms | 0 |

Both full-length runs delivered 324 progress updates and reached the upload
transport boundary. Hidden runs verified `document.visibilityState === 'hidden'`
throughout progress. Worker output at the first, batch, middle and partial-end
sector boundaries matched the original byte packer; the incremental audio ID
matched the native SHA-256 of the original full concatenation.

The eight-minute maximum-duration check also completed: four stems produced
168,521,728 encoded bytes. Foreground preparation/checksum took 1,254/4,507 ms;
the actually hidden tab took 5,442/20,368 ms. Both produced 1,288 progress updates,
matched the same sector/identity checks, and reported no main-thread task over
50 ms. These times include 4x CPU throttling. The first maximum-duration run
passed those measurements but hit a temporary-profile cleanup race after Chrome
exited; cleanup now retries temporary directory removal.

The incremental hash can take longer overall than native hashing, especially
in a background tab. It trades that bounded extra processing time for a
responsive interface and avoids large duplicate allocations. Heap observations
vary with garbage collection; they are measurements, not a memory guarantee.

## Other regression checks

`node --test site/tests/upload-preparation.test.js` covers:

- SHA-256 padding, block and 128 KiB chunk boundaries against Node's native hash;
- exact fallback sector bytes and four-stem identity with empty stems;
- cancellation before work and between batches;
- preflight rejection of an oversized later stem;
- the memory estimate without large allocations;
- blocked worker startup falling back, and runtime failure rejecting cleanly.

Existing library tests continue to check resumable prefixes, publication only
after flush/readback, unchanged other songs, and refusing malformed input.

## Limits

This checks preparation and identity computation, not the throughput or
reliability of a physical USB transfer. A hidden tab is covered; browser-discarded
or frozen tabs, a closed tab, unplugging USB and computer sleep are not. Browsers
may still throttle background work. No claim is made that a user's particular
long audio file or hardware upload was reproduced byte for byte.
