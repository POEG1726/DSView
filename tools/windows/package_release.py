# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a portable bundle from a clean source commit and an MSYS2 UCRT64 prefix.

No device access, installer actions, or modifications of existing installations.
Copyleft dependency source archives are mirrored as a separate Release asset.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import urllib.parse
import urllib.request
import zipfile


def run(args, **kwargs):
    return subprocess.check_output([str(arg) for arg in args], text=True, **kwargs)


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def sections(path):
    result, key = {}, None
    for line in path.read_text(encoding='utf-8').splitlines():
        if line.startswith('%') and line.endswith('%'):
            key = line.strip('%')
            result[key] = []
        elif line and key:
            result[key].append(line)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain-root', required=True, type=Path)
    parser.add_argument('--output-root', required=True, type=Path)
    parser.add_argument('--version', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    prefix = args.toolchain_root.resolve()
    msys = prefix.parent
    output = args.output_root.resolve()
    if output.exists():
        raise RuntimeError('Output directory exists; choose a new output path')
    if run(['git', 'status', '--porcelain'], cwd=root).strip():
        raise RuntimeError('Commit source changes before packaging')
    commit = run(['git', 'rev-parse', 'HEAD'], cwd=root).strip()
    if not re.fullmatch(r'\d+\.\d+\.\d+-mod\.\d+', args.version):
        raise RuntimeError('Expected derivative version such as 1.3.2-mod.1')
    output.mkdir(parents=True)
    name = f'DSView-{args.version}-windows-x64'
    bundle = output / name
    runtime = bundle / 'runtime'
    binary = runtime / 'bin'
    binary.mkdir(parents=True)
    env = dict(os.environ, PATH=str(prefix / 'bin') + os.pathsep + os.environ.get('PATH', ''))
    if args.version not in run([root / 'build.dir/DSView.exe', '--version'], env=env):
        raise RuntimeError('Built core version does not match requested release')
    shutil.copy2(root / 'build.dir/DSView.exe', bundle / 'DSView-core.exe')
    resource = output / 'launcher-version.o'
    run([prefix / 'bin/windres.exe', '-I', root, root / 'applogo.rc', '-O', 'coff', '-o', resource], env=env)
    run([prefix / 'bin/gcc.exe', '-O2', '-municode', '-mwindows',
         root / 'tools/windows/launcher.c', resource, '-o', bundle / 'DSView.exe'], env=env)
    flags = run([prefix / 'bin/pkg-config.exe', '--cflags', '--libs', 'Qt5Core'], env=env).split()
    run([prefix / 'bin/g++.exe', '-O2', '-std=c++11', root / 'tools/merge_loop_csv.cpp',
         '-o', binary / 'merge_loop_csv.exe', *flags, '-lzstd'], env=env)
    for folder in ('res', 'demo'):
        shutil.copytree(root / 'DSView' / folder, bundle / folder)
    shutil.copytree(root / 'libsigrokdecode4DSL/decoders', bundle / 'decoders',
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    if (root / 'lang').exists():
        shutil.copytree(root / 'lang', bundle / 'lang')
    python_lib = next((prefix / 'lib').glob('python3.*'))
    shutil.copytree(python_lib, runtime / 'lib' / python_lib.name,
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc', 'site-packages',
                        'test', 'tests', 'idlelib', 'turtledemo', 'tkinter', '_tkinter*',
                        '_curses*', '_test*', 'ensurepip', 'Tools', 'config-3.*'))
    plugin_root = prefix / 'share/qt5/plugins'
    for plugin in ('platforms/qwindows.dll', 'platforms/qoffscreen.dll',
                   'iconengines/qsvgicon.dll', 'imageformats/qsvg.dll'):
        target = runtime / 'plugins' / plugin
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(plugin_root / plugin, target)

    owners, packages = {}, {}
    for package_dir in (msys / 'var/lib/pacman/local').iterdir():
        if not (package_dir / 'desc').exists():
            continue
        desc = sections(package_dir / 'desc')
        if not desc.get('NAME', [''])[0].startswith('mingw-w64-ucrt-x86_64-'):
            continue
        package = desc['NAME'][0]
        packages[package] = desc
        for file in sections(package_dir / 'files').get('FILES', []):
            if file.startswith('ucrt64/') and not file.endswith('/'):
                owners[file.removeprefix('ucrt64/').casefold()] = package

    dlls = {file.name.casefold(): file for file in (prefix / 'bin').glob('*.dll')}
    system = Path(os.environ['SystemRoot']) / 'System32'
    used_packages, system_imports = set(), set()
    queue = [file for file in bundle.rglob('*') if file.suffix.lower() in ('.dll', '.pyd', '.exe')]
    scanned = set()
    # Python's DLL is required even if the executable import table uses an alias.
    python_dll = next((prefix / 'bin').glob('libpython3.*.dll'))
    shutil.copy2(python_dll, binary / python_dll.name)
    queue.append(binary / python_dll.name)
    while queue:
        file = queue.pop()
        if file in scanned:
            continue
        scanned.add(file)
        relative = file.relative_to(bundle).as_posix()
        origin = (relative.removeprefix('runtime/') if relative.startswith('runtime/') else '')
        if origin.startswith('plugins/'):
            origin = 'share/qt5/' + origin
        owner = owners.get(origin.casefold())
        if owner:
            used_packages.add(owner)
        imports = re.findall(r'DLL Name:\s*(\S+)', run([prefix / 'bin/objdump.exe', '-p', file]))
        for dependency in imports:
            key = dependency.casefold()
            if key in dlls:
                target = binary / dlls[key].name
                if not target.exists():
                    shutil.copy2(dlls[key], target)
                    queue.append(target)
                owner = owners.get(('bin/' + dlls[key].name).casefold())
                if owner:
                    used_packages.add(owner)
                else:
                    raise RuntimeError(f'Unowned runtime DLL: {dependency}')
            elif key.startswith(('api-ms-win-', 'ext-ms-win-')) or (system / dependency).exists():
                system_imports.add(dependency)
            elif not (bundle / dependency).exists():
                raise RuntimeError(f'Unresolved import {dependency} in {relative}')
    # Record ownership of the copied standard library, including its pure Python files.
    used_packages.add(owners[(f'lib/{python_lib.name}/os.py').casefold()])
    notices = bundle / 'licenses'
    shutil.copytree(prefix / 'share/licenses', notices / 'msys2')
    common = msys / 'usr/share/licenses/common'
    if common.exists():
        shutil.copytree(common, notices / 'common')
    for source, target in [('COPYING', 'DSView-COPYING'),
                           ('libsigrok4DSL/COPYING', 'libsigrok-COPYING'),
                           ('libsigrokdecode4DSL/COPYING', 'libsigrokdecode-COPYING'),
                           ('DSView/themes/LICENSE.md', 'themes-LICENSE.md'),
                           ('DSView/res/license.txt', 'firmware-resources-LICENSE.txt')]:
        shutil.copy2(root / source, notices / target)

    dependencies, sources = [], {}
    for package in sorted(used_packages):
        desc = packages[package]
        base, version = desc['BASE'][0], desc['VERSION'][0]
        filename = f'{base}-{version.split(":")[-1]}.src.tar.zst'
        source_url = 'https://repo.msys2.org/mingw/sources/' + urllib.parse.quote(filename, safe='')
        licenses = desc.get('LICENSE', [])
        # BSD/GPL dual-licensed Zstandard is distributed under its BSD option.
        needs_source = any('GPL' in license for license in licenses) and not (
            any('GCC-exception' in license for license in licenses) or base == 'mingw-w64-zstd')
        entry = dict(package=package, version=version, licenses=licenses,
                     homepage=desc.get('URL', [''])[0], source_url=source_url,
                     mirrored_source=filename if needs_source else None)
        dependencies.append(entry)
        if needs_source:
            sources[filename] = source_url
    source_dir = output / 'dependency-sources'
    source_dir.mkdir()

    def download(item):
        filename, url = item
        target = source_dir / filename
        print(f'Downloading dependency source: {filename}', flush=True)
        request = urllib.request.Request(url, headers={'User-Agent': 'DSView-release-packager'})
        with urllib.request.urlopen(request, timeout=120) as response, target.open('xb') as stream:
            shutil.copyfileobj(response, stream, length=1024 * 1024)
        # Validate the archive container without extracting or executing its contents.
        listing = run(['tar', '-tf', target])
        if 'PKGBUILD' not in listing:
            raise RuntimeError(f'Missing source build recipe: {filename}')
        return dict(filename=filename, sha256=sha256(target), bytes=target.stat().st_size, url=url)

    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        source_archives = list(pool.map(download, sorted(sources.items())))
    manifest = dict(version=args.version, source_commit=commit, architecture='windows-x64',
                    dependencies=dependencies, dependency_source_archives=source_archives,
                    windows_system_imports=sorted(system_imports))
    manifest_path = bundle / 'DEPENDENCIES.json'
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (bundle / 'README.txt').write_text(f'''DSView Modified {args.version} -- Windows x64 portable prerelease

Extract the whole ZIP into a writable folder, then open DSView.exe.
Keep DSView-core.exe and runtime/ beside the launcher. No development toolchain
is required. This package does not install USB drivers or change an existing
DSView installation. Existing DreamSourceLab DSView settings may be reused.
DSView-core.exe also honors DSVIEW_TEST_ISOLATION=1 when
launched with the runtime environment described in doc/windows-ucrt-build.md.

Requirements: 64-bit Windows 10/11 with the supported device driver installed
for physical capture. Windows 11 was used for local packaging checks; Windows
10, a separate clean machine and final physical capture remain unvalidated.
No driver installer is bundled. Obtain drivers from the instrument vendor.

Runtime self-check (no device access):
  DSView.exe --runtime-check runtime-check.json
The report covers Qt SVG icons, bundled resources, Python initialization and
decoder loading. It does not establish acquisition performance or reliability.

Segment merger: runtime/bin/merge_loop_csv.exe INPUT_DIRECTORY OUTPUT_CSV REPORT_JSON
The merger writes uncompressed output and never modifies source segments.

Source commit: {commit}
Corresponding source: DSView-{args.version}-source.zip on this Release page.
Copyleft runtime source/build recipes: DSView-{args.version}-dependency-sources.zip.
Other exact dependency source links and versions: DEPENDENCIES.json.
Copyright/license notices: licenses/. DSView is GPL-3.0-or-later; dependency
licenses, GCC runtime exceptions and the Zstandard BSD option remain applicable.
Release page: https://github.com/POEG1726/DSView/releases/tag/v{args.version}
''', encoding='utf-8')
    archive = output / f'{name}.zip'
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zipped:
        for file in sorted(bundle.rglob('*')):
            if file.is_file():
                zipped.write(file, file.relative_to(output))
    dep_archive = output / f'DSView-{args.version}-dependency-sources.zip'
    with zipfile.ZipFile(dep_archive, 'x', compression=zipfile.ZIP_STORED) as zipped:
        for file in sorted(source_dir.iterdir()):
            zipped.write(file, 'dependency-sources/' + file.name)
        zipped.write(manifest_path, 'DEPENDENCIES.json')
    source_archive = output / f'DSView-{args.version}-source.zip'
    run(['git', 'archive', '--format=zip', f'--prefix=DSView-{args.version}-source/',
         f'--output={source_archive}', commit], cwd=root)
    shutil.copy2(manifest_path, output / 'DEPENDENCIES.json')
    checksums = output / 'SHA256SUMS.txt'
    assets = [archive, dep_archive, source_archive, output / 'DEPENDENCIES.json']
    checksums.write_text(''.join(f'{sha256(file)}  {file.name}\n' for file in assets), encoding='ascii')
    print(json.dumps(dict(source_commit=commit, assets=[file.name for file in assets] + [checksums.name]), indent=2))


if __name__ == '__main__':
    main()
