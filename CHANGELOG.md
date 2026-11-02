# Changelog

## 1.3.2-mod.1 (prerelease) - 2026-11-02

Changes developed during 2026-10-31 through 2026-11-02, relative to upstream `2e9e2c8e726df4ef5687d39b83d4f797cc44b574`:

- Optimize transition-based logic CSV export and preserve snapshot ownership during concurrent export and capture.
- Add segmented Zstandard CSV recording in logic Stream/Loop mode, with bounded raw queues and an ordered parallel encoder pipeline.
- Preserve explicit incomplete-file status for overflow and write failures.
- Recover only the shared valid sample prefix when a manual stop leaves a partial interleaved group.
- Add offline integrity, corruption, tail, overflow and snapshot lifetime checks, plus optional benchmarks and a segment merger.
- Update Windows UCRT64 build dependencies and libusb event handling.
- Prepare public build, test and publication documentation; remove machine-specific paths from benchmark inputs and outputs.
- Identify the derivative in application and Windows file versions.
- Add `--runtime-check REPORT_JSON` to check Qt SVG icons, bundled resources, Python initialization and decoder loading without opening a device session.
- Add a portable Windows packaging script with dependency manifests, license notices, mirrored copyleft dependency sources and SHA-256 checksums.
- Register portable runtime DLLs for Python extension imports, and require all bundled decoders to load in the runtime check.
- Initialize the snapshot lifetime test through the same clear/setup path used by the application before feeding its first packet.

These changes are not an upstream DSView release. Historical test results and remaining validation limits are linked from the README.
