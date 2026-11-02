# SPDX-License-Identifier: GPL-3.0-or-later
param([string]$ToolchainRoot = (Join-Path $PSScriptRoot '..\build\toolchain\msys64\ucrt64'))
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$prefix = (Resolve-Path $ToolchainRoot).Path
$env:PATH=(Join-Path $prefix 'bin')+';'+$env:PATH
$env:QT_PLUGIN_PATH=Join-Path $prefix 'share\qt5\plugins'
$buildDir=Join-Path $projectRoot 'build\ucrt'
Push-Location $projectRoot
try {
    $flags=(& pkg-config --cflags --libs Qt5Core) -split ' ' | Where-Object { $_ }
    & g++ -O2 -std=c++11 'tests/loop_csv_recorder_test.cpp' 'DSView/pv/data/loopcsvrecorder.cpp' -o 'build/loop_csv_recorder_test.exe' @flags -lzstd
    if($LASTEXITCODE -ne 0){throw 'Recorder test compilation failed.'}
    & 'build/loop_csv_recorder_test.exe'; if($LASTEXITCODE -ne 0){throw "Recorder test failed: $LASTEXITCODE"}
    & g++ -O2 -std=c++11 'tests/loop_csv_parallel_integrity_test.cpp' 'DSView/pv/data/loopcsvrecorder.cpp' -o 'build/loop_csv_parallel_integrity_test.exe' @flags -lzstd
    if($LASTEXITCODE -ne 0){throw 'Parallel integrity test compilation failed.'}
    & 'build/loop_csv_parallel_integrity_test.exe'; if($LASTEXITCODE -ne 0){throw "Parallel integrity test failed: $LASTEXITCODE"}
    & g++ -O2 -std=c++11 'tests/loop_csv_requested_stop_test.cpp' 'DSView/pv/data/loopcsvrecorder.cpp' -o 'build/loop_csv_requested_stop_test.exe' @flags -lzstd
    if($LASTEXITCODE -ne 0){throw 'Requested-stop test compilation failed.'}
    & 'build/loop_csv_requested_stop_test.exe'; if($LASTEXITCODE -ne 0){throw "Requested-stop test failed: $LASTEXITCODE"}
    $include=(& pkg-config --cflags Qt5Widgets Qt5WinExtras glib-2.0 libusb-1.0) -split ' ' | Where-Object { $_ }
    & g++ -O2 -c 'tests/loop_csv_session_hook_test.cpp' -o 'build/loop_csv_session_hook_test.o' -IDSView -Ilibsigrok4DSL -Ilibsigrokdecode4DSL -Icommon -Ibuild/ucrt @include
    if($LASTEXITCODE -ne 0){throw 'Session-hook test compilation failed.'}
    $ninja=Get-Content (Join-Path $buildDir 'build.ninja')
    $link=$ninja | Where-Object { $_ -match '^build .+DSView\.exe: CXX_EXECUTABLE_LINKER__DSView_Release ' } | Select-Object -First 1
    if(!$link){throw 'Build the Release target with Ninja first.'}
    $objects=(($link -split ': CXX_EXECUTABLE_LINKER__DSView_Release ',2)[1] -split ' \| ',2)[0] -split ' ' | Where-Object { $_ -like '*.obj' -and $_ -ne 'CMakeFiles/DSView.dir/DSView/main.cpp.obj' }
    $libs=(($ninja | Where-Object { $_ -match '^  LINK_LIBRARIES = ' } | Select-Object -First 1) -split '= ',2)[1]
    $rsp=Join-Path $buildDir 'loop-session-test.rsp'; $obj=(Join-Path $projectRoot 'build\loop_csv_session_hook_test.o').Replace('\','/')
    (($objects -join ' ')+' "'+$obj+'" '+$libs) | Set-Content -Encoding ascii $rsp
    Push-Location $buildDir
    try { & g++ "@$rsp" -o '../loop_csv_session_hook_test.exe'; if($LASTEXITCODE -ne 0){throw 'Session-hook test linking failed.'} }
    finally {Pop-Location}
    & 'build/loop_csv_session_hook_test.exe'; if($LASTEXITCODE -ne 0){throw "Session-hook test failed: $LASTEXITCODE"}
} finally {Pop-Location}
