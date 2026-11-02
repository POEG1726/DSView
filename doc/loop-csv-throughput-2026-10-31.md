# Compressed CSV throughput and deployment — 2026-10-31

System: AMD Ryzen 7 5800H, 8 cores / 16 threads, 27.9 GiB usable RAM. Tests use the same recorder implementation as the Release executable and write actual compressed files on D:. They synthesize or replay input; they do not access DSLogic hardware or include USB reception and live display overhead.

## Changes

- Replaced the single serial CSV/gzip worker with a bounded encoder pool and an ordered writer.
- Reused row buffers, decimal formatting and precomputed channel suffixes instead of allocating a byte array for each transition.
- Switched compression to Zstandard level 1 with checksums. Results are concatenated independent frames, which decode to ordinary CSV.
- Preserved exact sample indices, configured sample-count splits, file-size limits and per-file initial states.
- On queue overflow, stop acquisition and save the buffered prefix; explicitly mark incomplete output.

## Measurements

The original stage benchmark produced 4,000,000 dense CSV rows (125 MiB): formatting 0.363 s, gzip 0.602 s, 23.3 MiB output. The complete serial path filled its 256 MiB queue in about 8.6 s at the dense 50 MHz workload. Parallel gzip alone still filled the queue; avoiding per-row allocation and switching to Zstandard were also necessary.

All following tests used 50,000,000 samples/s, six channels, 12 encoders and a 256 MiB raw queue:

| Input | Feed duration | Samples accepted and processed | Peak raw queue | Final drain | Compressed output |
|---|---:|---:|---:|---:|---:|
| CLK changes every two samples | 15 s | 750,780,416 | 4 MiB | 0.03 s | 548.2 MiB |
| CLK changes every sample | 15 s | 750,780,416 | 4 MiB | 0.06 s | 1,056.4 MiB |
| Six random channel words | 15 s | 750,780,416 | 4 MiB | 0.11 s | 3,997.9 MiB |
| Six random channel words | 70 s | 3,500,146,688 | 4 MiB | 0.17 s | 18,815.4 MiB |

The input source is paced by a monotonic clock at 50 MHz. The final count is rounded up to a complete input block, so total elapsed time slightly exceeds the requested feed duration. During the 70-second run, five-second observations showed no accumulating raw queue. Observed process peak working set during that run was 83.1 MiB. These are finite tests, not a guarantee for every duration, workload, competing CPU load, USB/device condition or disk condition.

## Integrity checks

- 65,536 samples and 4,238 transitions compared across 167 tiny segments, including packet fragmentation, concurrent stop, idle sample boundaries and queue overflow recovery.
- 1,048,576 samples and 963,353 transitions compared exactly with an independent generated reference using both one encoder and eight encoders; 226 files in each case. Includes idle spans, unaligned 100,003-point boundaries, timestamp checks and deliberately corrupted-frame rejection.
- Actual session packet-hook test confirms recording while display storage is locked and explicit marking of input overflow.
- Release build and installed launcher `--version` check passed.

Reproduce functional tests with `tests/run_loop_csv_tests.ps1`. The sustained test source is `tests/loop_csv_dense_realtime_test.cpp`; its current CLI takes the output directory, optional duration seconds, encoder count (0 means automatic), and pattern (1 toggles every sample, 2 every two samples, 3 random channel words). See [test instructions](../tests/README.md). The table above records historical tests with the earlier CLI. Link the recorder with Qt5Core and libzstd.

## Installed result

Only `C:/Program Files/DSView/DSView-core.exe` was replaced. SHA-256:

`DBA9C4C5B2FE47AB416C0DC02ABAB227192A69DA92238D24BE06AA0E7F86A1D1`

The existing launcher and runtime remained in place. Installed libzstd.dll matched the build runtime by SHA-256. All 11 files in the local DSView profile matched their immediately preceding hashes after deployment. No profile/configuration file was copied over. This is historical deployment evidence, not a portable release artifact or current installation check. No device test of this recorder revision was performed.
