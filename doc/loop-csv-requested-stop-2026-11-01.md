# Requested-stop tail fix — 2026-11-01

A reported manual stop during a six-channel 50 MHz Loop recording produced an incomplete-interleaved-block error. USB packets need not end at the 48-byte boundary used for six channels × 64 samples. The recorder previously treated every unfinished group as an unexpected termination.

The session now marks intentional stop before stopping the device. At normal end, a marked recorder drains its queued prefix and recovers only the final samples whose bytes are present for every channel. For example, a 41-byte six-channel tail has five full channel words and one byte for the sixth channel: eight further samples can be saved, while 35 unmatched bytes are omitted. For a shorter tail lacking any final-channel bytes, no extra samples are invented. The final compressed file records these statistics and the exact exclusive end sample and is renamed from .part to .csv.zst. Unexpected tails and already-reported errors remain incomplete.

Regression checks passed:

- All 184 possible tail lengths for 1, 6 and 16 channels, including aligned stops, fragmented packets, size/sample splits, exact channel states and recovery bounds.
- 65,536 samples / 4,238 transitions across 271 tiny segments; unexpected tail still fails; overflow still fails even when a stop was requested.
- Single- and eight-encoder random reconstruction: 1,048,576 samples / 963,353 transitions each.
- Session stop-request hook tested with an uninitialized device context and no collection thread: it marks stop before finalizing. No hardware open, read or capture was performed.
- Corruption detection and actual packet-hook overflow checks remain enabled.

A tiny-file regression initially found that the enlarged footer reserve left insufficient allowance for growing start-index digits. The page budget now additionally reserves 20 bytes for that header growth; the entire suite passed after the correction.

Release core installed at C:/Program Files/DSView/DSView-core.exe:

`37B8C9BFA39A3F513E0480FB431138745961BC1D6426B0D59AEF7946B550519D`

The installed launcher --version check passed. All eleven profile-file hashes matched their pre-update values; launcher/profile/runtime files were not replaced. These are historical local deployment results. The corrected revision was not tested with a physical device.

## Existing merged recording

The merged CSV was compressed to capture_merged.csv.zst: 2,277,474,088 bytes (2.121 GiB). Full decompression matched the original 13,149,998,946-byte CSV SHA-256 1ac76685febc5286c75dc85f64e3c256c6c99a504bb80b0219341212a10e582e. The generated uncompressed copy was removed after successful verification; the ten original capture segments remain. The missing original tail end marker is still explicitly reported; this fix does not fabricate a historical bound.
