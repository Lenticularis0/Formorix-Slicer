# Formorix Slicer development

This repository is the Formorix fork of
<https://github.com/OrcaSlicer/OrcaSlicer>. The complete upstream Git history,
`LICENSE.txt`, third-party license files and upstream credits are retained.
Formorix additions are supplied under AGPL-3.0-or-later. See the About dialog
and [5D architecture](HLSD/formorix-5d.md).
The integrated surface editor is described in
[surface editor architecture](HLSD/formorix-surfaces.md).
The experimental nonplanar preview is described in
[preview architecture](HLSD/FormorixPreview.md). The latest Windows build,
GUI acceptance checks and remaining limits are recorded in
[Auftrag 03 acceptance report](Formorix-Auftrag-03.md).
User help and a reproducible example start at [Formorix help](formorix/index.md).

## Remotes and branches

```text
origin    https://github.com/Lenticularis0/Formorix-Slicer.git
upstream  https://github.com/OrcaSlicer/OrcaSlicer.git
```

The initial development branch is `formorix/auftrag-01`, based on upstream
`main` at `f55615d7488fe570942f5e25548b8ab9b30778d0` (2.6.0-dev).
`Lenticularis0/Formorix` is a separate CAD repository and is not used by this build.

To review a later upstream update, first commit local changes, then create an
integration branch, fetch upstream and merge the chosen upstream revision.
Re-run the build, existing tests and the Formorix tests before updating the fork's
default branch. Do not replace the fork with an upstream release executable.

```powershell
git fetch upstream --tags
git switch -c integration/orca-update
git merge upstream/main
```

## Windows build

Use the upstream `build_win.bat`. Required tools are Visual Studio C++ x64,
Windows SDK, CMake, Perl and Git. Python 3 is needed for profile and localization
tools. Keep all original optional features enabled for the baseline build.
The first dependency build downloads and compiles substantial third-party code.

```powershell
.\build_win.bat -d --msvc --msbuild -j 8
.\build_win.bat -s --msvc --msbuild --tests -j 8 --install
.\build\tests\slic3rutils\Release\tools\uv\uv.exe pip install --python .\build\tests\slic3rutils\Release\python\python.exe --only-binary :all: numpy==2.5.3
ctest --test-dir build/tests -C Release -LE 'NotWorking|RequiresApp' --no-tests=error --output-on-failure
.\build\tests\libslic3r\Release\libslic3r_tests.exe '[Formorix5D]'
.\build\tests\fff_print\Release\fff_print_tests.exe '[Formorix5D]'
python scripts/orca_profile_tool.py check
python scripts/orca_profile_tool.py version-check --base upstream/main
```

Internal CMake targets and executable filenames retain upstream names for build
compatibility. The running application identifies itself as Formorix Slicer.
Local build trees, downloaded dependency sources and logs are not source artifacts.
Close test instances before building/installing: the upstream post-build step
refreshes the bundled Python runtime, which Windows cannot replace while loaded.
For incremental Windows builds with limited memory, use
`cmake --build build --config Release --target ALL_BUILD --parallel 2 -- /p:CL_MPCount=2`.
Limit both MSBuild and compiler parallelism; the upstream `/MP` setting can otherwise
exhaust the commit budget while compiling precompiled headers. A separate installation
can be made with `cmake --install build --config Release --prefix build/Formorix-Auftrag03`.
NumPy above is installed only in the isolated test interpreter for the existing
Python bridge tests. Windows skips symlink tests when the current process lacks
symlink privileges; the Linux-only CLI suite is not registered on Windows.

## Manual verification

1. Launch the installed build and verify the title and About attribution.
2. Add an ordinary existing printer, load a cube, slice and export planar G-code.
3. Install the Formorix vendor/model in the existing printer selection wizard.
4. Open the printer settings and the **Formorix 5D** page. Edit a different X/Z/A/B
   value and extrusion channel for each head, and save a user printer preset.
5. Restart and verify the preset values; save/reopen an editable 3MF project too.
6. With experimental mode enabled, verify slicing, G-code/print-job export and
   sending are blocked. A project can be saved, but must contain no `.gcode` file.
7. Disable experimental mode on a copy to return to the conventional pipeline.
   Its output is planar and does not represent three-head operation.

The Formorix preset is intentionally rejected by the upstream validator's
`validate_slice` sweep: accepting it as an executable print job would violate
the configuration-only contract. Static profile validation and the dedicated
rejection test are the appropriate checks for this preset. All upstream vendor
profiles are retained without edits.

## Geometry and process foundation tests

The independent double-precision geometry, nominal material deposition, three-head
process profiles and manufacturing dependencies are described in
[FormorixFoundations](HLSD/FormorixFoundations.md). They do not enable hybrid slicing
or executable 5D export. The [Auftrag 03B report](Formorix-Auftrag-03B.md) records
measured errors, supported cases and remaining limits.

```powershell
build/tests/libslic3r/Release/libslic3r_tests.exe '[FormorixFoundations],[FormorixSurfaces]'
```
