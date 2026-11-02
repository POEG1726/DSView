# DSView Modified

An unofficial derivative of [DreamSourceLab/DSView](https://github.com/DreamSourceLab/DSView), focused on logic capture export and continuous compressed CSV recording. DSView supports DreamSourceLab instruments and is based on the [sigrok project](https://sigrok.org).

This source tree starts from upstream commit `2e9e2c8e726df4ef5687d39b83d4f797cc44b574` (2024-11-05). It retains the upstream history and copyright notices. This derivative is maintained independently of DreamSourceLab; upstream releases and support statements do not describe this version.

## Changes in this version

- Faster transition-based logic CSV export, with sample-index preservation.
- Background logic CSV, VCD and gnuplot exports that retain the old snapshot while a new capture starts. One save/export task may run at a time; other save modes remain modal.
- Loop recording to segmented `.csv.zst` files, with bounded raw queues, parallel encoders, ordered output and frame checksums.
- Explicit incomplete-file handling for overflow and write errors, plus manual-stop tail recovery that only emits samples present on every channel.
- A command-line [segment merger](doc/loop-csv-recording.md#merging-segments) and offline regression tests.
- Windows UCRT64 build changes, including Qt SVG and WinExtras dependencies and a timed libusb event fallback.

## Build and use

The locally exercised build is Windows x64, MSYS2 UCRT64 and Qt 5. See [Windows build and runtime setup](doc/windows-ucrt-build.md). Building this source does not replace an installed application.

Windows portable prereleases and corresponding source are available on the [Releases page](https://github.com/POEG1726/DSView/releases). Extract the full Windows archive and run `DSView.exe`; no development toolchain is required. Device drivers are obtained separately from the instrument vendor.

For continuous recording, select logic **Stream** mode, then **Loop**, and enable **Loop: record CSV to disk...** in the Mode menu. See [recording format, configuration and recovery limits](doc/loop-csv-recording.md).

## Validation status

The export and recorder changes have offline regression coverage. Historical performance measurements and their conditions are recorded in [export verification](doc/export-verification-2026-10-31.md), [recorder throughput](doc/loop-csv-throughput-2026-10-31.md) and the [manual-stop fix](doc/loop-csv-requested-stop-2026-11-01.md).

Synthetic 50 MHz recorder tests do not establish end-to-end USB capture performance. A separate historical 10 MHz physical-device test covered export/capture overlap; it did not validate the final Loop recorder or establish 50 MHz hardware reliability. Linux, macOS, Qt 6 and DScope behavior have not been revalidated for these changes. This derivative reports `1.3.2-mod.1`; release notes identify the exact source commit.

See [test instructions](tests/README.md), [change history](CHANGELOG.md), [contribution guidance](CONTRIBUTING.md) and the [publication workflow](doc/releasing.md).

## Copyright and license

The complete DSView program is licensed under the GNU General Public License, version 3 or later (`GPL-3.0-or-later`); see [COPYING](COPYING). Some individual files have GPLv2-or-later or other compatible notices. Retain those notices and consult the individual files for their copyright holders and terms. Third-party code, themes, firmware resources and runtime components retain their own applicable notices.

Publishing this derivative in a fork or in a standalone repository does not replace those license obligations. Binary releases must provide the corresponding modified source and the build/packaging material needed to rebuild them.
