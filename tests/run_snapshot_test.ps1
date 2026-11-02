# SPDX-License-Identifier: GPL-3.0-or-later
param([string]$ToolchainRoot = (Join-Path $PSScriptRoot '..\build\toolchain\msys64\ucrt64'))
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$toolchain = (Resolve-Path $ToolchainRoot).Path
$env:PATH = (Join-Path $toolchain 'bin') + ';' + $env:PATH
$buildDir = Join-Path $projectRoot 'build\ucrt'
$ninja = Get-Content (Join-Path $buildDir 'build.ninja')
$link = $ninja | Where-Object { $_ -match '^build .+DSView\.exe: CXX_EXECUTABLE_LINKER__DSView_Release ' } | Select-Object -First 1
if (!$link) { throw 'Build the Release target with Ninja first.' }
$objects = (($link -split ': CXX_EXECUTABLE_LINKER__DSView_Release ', 2)[1] -split ' \| ', 2)[0] -split ' ' | Where-Object { $_ -like '*.obj' -and $_ -ne 'CMakeFiles/DSView.dir/DSView/main.cpp.obj' }
$libs = (($ninja | Where-Object { $_ -match '^  LINK_LIBRARIES = ' } | Select-Object -First 1) -split '= ', 2)[1]
$response = Join-Path $buildDir 'lifetime-test.rsp'
$testObj = Join-Path $projectRoot 'build\export_snapshot_lifetime_test.o'
$testExe = Join-Path $projectRoot 'build\export_snapshot_lifetime_test.exe'
$flags = @('-O2', '-c', (Join-Path $PSScriptRoot 'export_snapshot_lifetime_test.cpp'), '-o', $testObj, "-I$projectRoot\DSView", "-I$projectRoot\common", "-I$projectRoot\libsigrok4DSL", "-I$projectRoot\libsigrokdecode4DSL", "-I$buildDir")
$pkgflags = (& pkg-config --cflags Qt5Widgets glib-2.0 libusb-1.0) -split ' ' | Where-Object { $_ }
& g++ @flags @pkgflags
if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
($objects -join ' ') + ' "' + $testObj.Replace('\','/') + '" ' + $libs | Set-Content -Encoding ascii $response
Push-Location $buildDir
try {
    & g++ "@$response" -o $testExe
    if ($LASTEXITCODE -ne 0) { throw 'Test linking failed.' }
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw 'Snapshot lifetime test failed.' }
} finally { Pop-Location }
