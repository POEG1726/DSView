# Contributing

This is an independently maintained DSView derivative. File issues and pull requests against this repository for its changes; identify the upstream project separately when reporting an upstream issue.

## Development

Use [the Windows UCRT64 setup](doc/windows-ucrt-build.md), then run the relevant [offline tests](tests/README.md). Include your source commit, OS, toolchain, test command and result in a pull request. Keep changes focused so export optimization, recorder behavior and platform fixes can be reviewed independently.

Physical-device tests are separate from offline tests. Record device model, USB connection, channel count, sample rate, duration, input characteristics and observed errors. A clean synthetic test or short physical run does not establish loss-free operation under other conditions.

## Test data and reports

Do not commit local profiles, private captures, generated output, crash dumps or installed runtime directories. Prefer synthetic fixtures. If a report uses a private recording, describe its source and acquisition time, state that the input is not distributed, and distinguish historical results from tests of the current commit. Review logs and merge reports for absolute paths before sharing them.

## Licensing

Preserve existing copyright and license notices. Contributions must be compatible with the whole program's GPL-3.0-or-later terms and any applicable file-level terms. Do not replace third-party attribution with a link alone. Include notices and source information for new dependencies.
