# SoundTouchJS 2.1.1

Bonsai 8 uses `@soundtouchjs/core` and `@soundtouchjs/interpolation-strategy-lanczos`, both pinned to **2.1.1**, under the Mozilla Public License 2.0. Copyright notices remain in the distributed files.

- [Full license](./MPL-2.0.txt)
- [Corresponding TypeScript source, package metadata and lockfile](./source-2.1.1.tar.gz)
- [Original core npm package](./package-2.1.1.tgz)
- [Original Lanczos npm package](../soundtouch-lanczos-2.1.1/package-2.1.1.tgz)
- [Upstream release source](https://github.com/cutterbl/SoundTouchJS/tree/d9e39a7ddcf74c7a145bbba45856f30068c82b79)
- [Upstream processing documentation](https://github.com/cutterbl/SoundTouchJS/blob/v2.1.1/packages/core/README.md)

The only runtime vendor modification is in `interpolationStrategyRegistry.js`: the bare npm import becomes `../soundtouch-lanczos-2.1.1/index.js` for static hosting. The modified JavaScript is distributed unminified alongside this notice. The source archive contains the unchanged upstream packages and build metadata; apply that one import substitution after compilation to reproduce the hosted module layout.

Downloaded package SHA-512 integrity was checked before extraction:

```
core: sha512-ccBiG9/zHdM1Sx1RUkXWBgRYFI1HCiUHHwRK8gAvVNgmcafBgUzRP6Czn7pUPcHgSQus+i3Eu3DlfBR24wqspA==
lanczos: sha512-EX4qVLnOJ9pKfL7iSPJSJDKDAoZ4d7JO7KTKMPVo2XIhlmT/2hTZC5dWL/R9Jv3XC7kyQeteeGcogWP6DPi47A==
```

Bonsai's own matching adapter uses shared-stereo WSOLA overlap decisions and Lanczos transposition. Pitch sets the transposition ratio; the public stretch stage receives `tempo / pitch`, so duration and pitch vary independently. It renders offline in a terminable worker, pads input to flush the processor, trims the preroll, and returns the requested duration. No formant preservation is promised. Independent stems can have small transient offsets; the four stems are not processed as one phase-locked multichannel signal. Correct grids and listen before exporting. These prepared files can be uploaded; this is not live automatic matching inside the physical SP-1.
