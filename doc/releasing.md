# Publication and release workflow

## Repository approach

Use a GitHub fork of [DreamSourceLab/DSView](https://github.com/DreamSourceLab/DSView) for the current enhancements. Retain the upstream Git history. A fork can have its own README, branches, issues and releases while preserving the upstream connection for synchronization and pull requests.

A standalone repository is reasonable if the project later has a distinct identity and maintenance direction. Preserve provenance, preferably the Git history, and retain all applicable licenses in that case too. A reference link alone is not a substitute for copyright, license or corresponding-source requirements.

References: [GitHub forks](https://docs.github.com/en/pull-requests/reference/forks), [fork remotes](https://docs.github.com/en/pull-requests/how-tos/work-with-forks/configuring-a-remote-repository-for-a-fork) and [GNU corresponding-source guidance](https://www.gnu.org/licenses/gpl-faq.en.html#DistributeExtendedBinary).

## Maintained repository

The maintained fork is [POEG1726/DSView](https://github.com/POEG1726/DSView). For a fresh checkout:

```sh
git clone https://github.com/POEG1726/DSView.git
cd DSView
git remote add upstream https://github.com/DreamSourceLab/DSView.git
```

Keep `origin` pointing to the maintained fork and `upstream` pointing to DreamSourceLab. Preserve the existing history and local work when configuring an existing checkout. The baseline is `2e9e2c8e726df4ef5687d39b83d4f797cc44b574`; fetch and review upstream changes separately rather than merging unrelated updates into a release.

Before committing, review the complete patch and the untracked files. Stage intended source, test, tool and documentation paths explicitly; do not start with a blanket `git add .`. If practical, group changes into export/snapshot handling, Loop recording/manual-stop recovery, platform fixes, and publication documentation. Do not reset existing work just to manufacture that split. The public default branch should contain the maintained derivative; use feature branches for proposed work and tags for releases.

## Source publication checks

- Preserve upstream history, COPYING files, copyright notices and file-level licenses.
- Confirm README describes the modified version, baseline, limitations and actual build requirements.
- Run a clean build and the documented offline tests against the commit being published.
- Review the candidate tracked file list and patch for personal paths, credentials, captures and generated files. `.planning/`, `build*`, `captures/`, `test-results/` and `release/` are ignored locally; ignore rules do not remove files already tracked or historical commits.
- Keep private benchmark recordings out of the repository. Historical reports may remain when their source, test time and limitations are clear.
- Review the staged diff and file list before committing and pushing. Use the maintainer's configured Git author and committer identities, and preserve the original authors of upstream commits.

## Binary releases

Portable Windows prereleases are assembled from committed source and the matching MSYS2 UCRT64 packages. The local installed directory is not a release artifact. The standalone launcher source is [tools/windows/launcher.c](../tools/windows/launcher.c); its runtime layout is described in the build guide.

For each binary release:

1. Build from a clean tagged source commit; record toolchain and dependency versions and distinguish the derivative from upstream v1.3.2 in version information.
2. Package the actual runtime dependencies, Qt platform/SVG plugins, Python standard library, decoder scripts and DSView resources. Include applicable dependency and resource license notices and corresponding-source information. Review redistribution terms for any vendor driver/installer material separately before including it.
3. Include all modified source and build/packaging scripts corresponding to the binary; a link to unmodified upstream source is insufficient. Provide equally accessible source from the release page and identify the source tag/commit.
4. Test startup on a Windows machine without the development toolchain. A launcher `--version` check alone does not validate plugins, Python decoders or the main interface.
5. Record actual device validation separately from offline checks. Publish SHA-256 hashes and release notes with known limitations.

## Portable package command

After building the committed source in UCRT64, use Python 3.11 or newer from the repository root (replace the toolchain prefix):

```powershell
python tools/windows/package_release.py --toolchain-root C:\msys64\ucrt64 --output-root release/v1.3.2-mod.1 --version 1.3.2-mod.1
```

The script requires a clean Git working tree and a new output directory. It assembles resources and the Python standard library from source/package files, computes the PE import closure against the selected prefix, includes Qt platform/SVG plugins and license notices, and downloads exact copyleft dependency source packages from MSYS2. It does not copy local profiles, vendor driver installers or an existing DSView installation. The package includes a merger at `runtime/bin/merge_loop_csv.exe`.

Outputs are the Windows portable ZIP, corresponding application-source ZIP, mirrored dependency-source ZIP, `DEPENDENCIES.json` and `SHA256SUMS.txt`. Dependency manifests contain package versions, applicable license metadata, source locations and mirrored-source hashes. Zstandard uses its BSD license option; GCC runtime libraries retain their runtime exception.

Before uploading, extract the Windows ZIP into a new directory, restrict PATH to Windows system directories and run `DSView.exe --runtime-check REPORT_JSON`. Confirm `passed: true` and that the report identifies the intended derivative version. This check runs before constructing an application/device session; it tests GUI runtime initialization, SVG icons, firmware-resource presence, embedded Python and decoder loading. It does not validate actual acquisition or replace a separate-machine test.

`v1.3.2-mod.1` remains a prerelease because a separate clean-machine and final physical-device validation have not been performed. Local restricted-environment checks and offline regressions are recorded in its release notes. Do not promote it to a stable hardware-validated release based on those checks alone.
