# Loop compressed CSV recording

In logic Stream mode, select **Loop**, open the Mode menu, and enable **Loop: record CSV to disk...**. Select an output directory, the per-file **uncompressed CSV** limit (1-16384 MiB, default 1024), the raw write queue (8-4096 MiB, default 256), and maximum samples per segment (default 3,000,000,000). A segment closes when either limit is reached. Sample-count boundaries are anchored to the start of the capture; an earlier size split can produce a shorter segment. This is independent of wall-clock time.

Output is `capture_*.csv.zst`, containing standard CSV compressed with Zstandard level 1. Independent compressed frames include checksums. Input packets remain bit-packed raw data; the recording receiver copies them before display storage can recycle them. A bounded pool converts and compresses small sample ranges in parallel, and one writer commits the results strictly in sample order. On this 16-thread system the default pool uses 12 workers. The raw queue is not an estimate of total process memory; workers use additional bounded buffers.

Each file starts with the full channel state and an inclusive sample index, and ends with an exclusive sample index. `Sample` is the exact index since the beginning of the run. CSV timestamps are exact decimal fractions for rates dividing a power of ten up to 1 GHz; other rates use floating point text. The sample index remains authoritative. File boundaries may repeat an initial state so each file can be interpreted independently.

An active segment is `.csv.zst.part`. An intentional capture stop is marked before asking the device to stop. If its last cross-channel group is incomplete, the recorder saves only the samples actually present on every channel, records the tail byte count/recovered samples/omitted unpaired bytes, writes the exact final bound, and closes normally. Padding is never emitted as invented samples. A partial group without a stop request still reports an error; a stop request does not clear overflow, disconnect or write errors. A successful close flushes it and renames it to `.csv.zst`. A crash can leave an incomplete last frame; earlier complete frames can be recovered with an appropriate streaming decoder. Checksums detect corruption but cannot recover missing bytes. `QFile::flush` does not guarantee survival of a power loss.

On queue overflow, capture stops and the writer drains the already buffered prefix, including the full block that triggered the stop. Data after that prefix may be missing, so the last file remains `.part`. Overflow, invalid input, USB overflow, disconnect and disk errors must not be treated as a complete recording. Increasing the queue cannot fix a sustained throughput deficit.

Offline validation and historical deployment results are in [loop-csv-throughput-2026-10-31.md](loop-csv-throughput-2026-10-31.md). The final recorder and manual-stop changes were validated offline; no physical-device validation of that revision is recorded.

## Merging segments

Build the merger from the repository root in an MSYS2 UCRT64 shell:

```sh
g++ -O2 -std=c++11 tools/merge_loop_csv.cpp -o build/merge_loop_csv.exe $(pkg-config --cflags --libs Qt5Core) -lzstd
./build/merge_loop_csv.exe INPUT_DIRECTORY OUTPUT_CSV REPORT_JSON
```

Use a stopped recording and new output/report paths. The tool reads `capture_*.csv.zst` and `capture_*.csv.zst.part` in filename order, validates frames and segment continuity, removes redundant initial-state rows, and writes an **uncompressed** CSV plus a JSON report. It leaves source segments unchanged and refuses to replace existing output files. Allow enough disk space for the uncompressed result. The report includes absolute source paths; review those before sharing it.

Only the last segment may lack an end marker, and only when it is a `.part` file whose compressed frames and CSV rows are complete. A truncated compressed frame or invalid continuity is rejected. If the last source has no end marker, the final capture endpoint remains explicitly unknown; the merger does not invent it from the last transition.
