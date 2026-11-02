# DSView export and concurrent capture verification — 2026-10-31

## Real capture-file export comparison

Source: a private `DSLogic U2Pro16-la-261031-203400.dsl` recording, acquired on 2026-10-31 at 20:34 in the recording machine's local time. The input is not distributed. These are historical results from 2026-10-31, not measurements of every later revision. The archive header records 17,179,869,184 as its configured sample count; DSView actually loaded 853,561,728 samples, six channels at 50 MHz. Both runs used that same loaded snapshot and the same newly built program. `DSVIEW_BENCH_LEGACY` selects the former per-sample reconstruction/export path; it is not a timing of the installed vendor executable. The comparison includes the existing roughly 300 ms task-completion delay and timer polling, and does not include loading the capture.

- Legacy path: 13,185 ms; optimized compressed CSV path: 446 ms (29.6x elapsed-time ratio).
- Both outputs are 65,926 bytes and have SHA-256 `487C866E2AC817B9D3C96D3E3BE4A9C656E6CC7F5E2ED1FF78B8E7A35B2E4404`.
- Optimized task reported scan 97.503 ms, formatting/stream 2.888 ms, processing plus flush 101.453 ms.
- Logs: `build/actual-benchmark.out`, `build/actual-benchmark.err`. Outputs: `build/benchmark-legacy.csv`, `build/benchmark-fast.csv`.
- The earlier 28 MB CSV was not available in this environment; these times are not a measurement of that CSV or of the installed binary.

## Physical-device concurrent capture

Test used DSLogic U2Pro16 over a USB 2.0 connection, streaming at 10 MHz, 16 channels. First capture had 89,686,016 samples; the second capture had 29,628,416. For this concurrency test only, `DSVIEW_BENCH_CHUNK_DELAY_MS=5` injected a 5 ms pause per optimized-export chunk so the export remained active long enough to overlap real device transfer. At 68 ms after the second start call returned, 200,192 samples had reached the new snapshot while the first snapshot's export processing was still active. Reference and concurrent exports were both 238 bytes, with SHA-256 `4F799ABA155C41A1AB044CCC42C01663B7F38A38F3A49FDB022DB7524229EE95`. The signal was nearly constant: this validates snapshot ownership during this run, not high-transition output, sustained throughput, or absence of lost data.

Logs: `build/hardware-benchmark-3.out`, `build/hardware-benchmark-3.err`. Outputs: `build/hw-reference.csv`, `build/hw-concurrent.csv`. The test driver invoked the actual application session and StoreSession APIs directly; Windows UI automation failed to bind the window, so an end-user click sequence in the progress dialog was not tested.

An initial physical test crashed before capture due to the stock Windows libusb `libusb_get_pollfds()` returning NULL. The Windows fallback pumps libusb events from a timed session source instead. The revised 10 MHz device tests completed and released the device. This does not establish behavior at 50 MHz or for DScope devices.

A separate serious regression was found: Qt defines `emit` as a macro, which stripped the sparse scanner callback in the prior GUI build; rename to `on_edge` fixes it. The scanner test now includes Qt headers, and the real capture output matches the legacy output. Do not use the older `build/test-install` application for this test. The corrected isolated launcher is `build/test-install-fixed/run-DSView-test.cmd`.
