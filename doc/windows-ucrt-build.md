# Windows UCRT64 build

The locally exercised configuration uses MSYS2 UCRT64, GCC 16.2.0, CMake 4.4.5, Qt 5.15.19, GLib 2.90.1, libusb 1.0.30 and Zstandard 1.5.7. These identify the development environment, not strict minimum versions. Qt 6 and non-Windows builds have not been revalidated for these changes. The older cross-platform [INSTALL](../INSTALL) file remains upstream guidance and is not the validated setup for this derivative.

## Configure and build

In an MSYS2 **UCRT64** shell, install the dependencies:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-qt5-base mingw-w64-ucrt-x86_64-qt5-svg mingw-w64-ucrt-x86_64-qt5-winextras mingw-w64-ucrt-x86_64-glib2 mingw-w64-ucrt-x86_64-libusb mingw-w64-ucrt-x86_64-fftw mingw-w64-ucrt-x86_64-boost mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-zstd
```

From the repository root:

```sh
cmake -S . -B build/ucrt -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build/ucrt
```

The current CMake project puts the executable at `build.dir/DSView.exe`, even for this out-of-source build. It does not update an installed application. The policy argument permits this older project's CMake minimum version with CMake 4.x.

Run the [offline tests](../tests/README.md) after building. The PowerShell runners take `-ToolchainRoot`, for example `C:\msys64\ucrt64`; their fallback is a developer-local toolchain under `build/toolchain/msys64/ucrt64`, which is not included in the repository.

## Runtime layout

The built executable depends on the UCRT64 DLLs; copying it over a vendor installation alone is insufficient. For development, use an isolated runtime directory, with the following layout if using the optional launcher:

```text
DSView.exe                 # optional launcher
DSView-core.exe            # renamed build.dir/DSView.exe
res/                       # DSView/res
demo/                     # DSView/demo
lang/                      # runtime language resources, as applicable
decoders/                  # libsigrokdecode4DSL/decoders
runtime/
  bin/                     # matching runtime DLLs
  plugins/
    platforms/qwindows.dll
    iconengines/qsvgicon.dll
    imageformats/qsvg.dll
  lib/                     # matching Python standard library
```

The [launcher source](../tools/windows/launcher.c) sets its child's PATH, PYTHONHOME and QT_PLUGIN_PATH relative to its own directory. It forwards command-line arguments and waits for the core's exit status. Compile it in UCRT64 with:

```sh
gcc -O2 -municode -mwindows tools/windows/launcher.c -o build/DSView-launcher.exe
```

Rename the launcher to `DSView.exe` only within the isolated runtime directory. Use DLLs, Python standard library and Qt plugins from the same toolchain as the build. The diagram specifies the layout, not an exhaustive dependency inventory or an automated redistribution package. See [release preparation](releasing.md) before publishing binaries.

Without the launcher, set PATH to include the matching UCRT64 `bin`, PYTHONHOME to the toolchain prefix, and QT_PLUGIN_PATH to its `share/qt5/plugins` for that process. Put the DSView resources and decoders beside the executable as above. `DSVIEW_TEST_ISOLATION=1` makes the core use the `DSView-test` settings namespace. The optional system-style launcher clears that variable and uses normal DSView settings, so use the core directly for isolated development.

The platform plugin is required for GUI startup. The SVG plugins are also required: missing `qsvgicon.dll` or `qsvg.dll` can leave title-bar and toolbar icons blank. `--version` exits before the GUI initializes and does not test these runtime components.
