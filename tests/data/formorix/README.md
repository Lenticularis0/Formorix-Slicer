# Formorix native project fixtures

## `auftrag05-three-materials.3mf`

This native Orca/Formorix project is generated and round-tripped by
`[FormorixMultiMaterial][NativeRoundTrip]` in
[`test_formorix_multi_material.cpp`](../../libslic3r/test_formorix_multi_material.cpp).
Open it **as a project**, including its settings, to import the nine embedded
Orca printer, filament and process presets. Importing only its mesh does not
provide those profile sources.

The original body is 18 × 4 × 5 mm, placed at X=60, Y=40 mm. Its top surface
contains three adjacent painted 6 × 4 mm rectangles. Each rectangle belongs to
one independent head and retains its own process and material bindings:

| Head | Material source | Nozzle | Requested line width | Layer height | Layers | Target skin volume |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | PETG | 0.8 mm | 0.9 mm | 0.25 mm | 2 | 12 mm³ |
| 2 | PVA | 0.4 mm | 0.45 mm | 0.125 mm | 3 | 9 mm³ |
| 3 | PETG-CF | 0.6 mm | 0.7 mm | 0.1875 mm | 4 | 18 mm³ |

Explicit local width limits permit the nominal rectangular strips to fill each
region. The original filament, process and outer-wall flow factors are retained.
The native filament IDs are `A`, `B` and `C`, respectively; they are explicit
profile metadata and remain distinct after native import.
The reference skin volume is 39 mm³ and the remaining core volume is 321 mm³.
The test compares these volumes, the source mesh, all painted regions, resolved
profiles, spatial bounds and the three head tracks before and after native
archive loading.

The saved informational process snapshots are deliberately obsolete. The test
uses the same embedded-preset and full-configuration import sequence as the GUI,
invalidates those snapshots and resolves all three heads from the current Orca
sources. Fresh source TCP offsets are zero; the old cached offsets are not used.
The archive embeds those sources through the same `append_project_sources`
helper used by GUI project saving, including inactive head assignments and
current edits. Existing same-name sources with different settings are rejected
on import rather than silently replacing the archived definitions. Resolve such
a conflict explicitly before opening the fixture; its source names begin with
`Formorix reference head`.
The project has no analytic CAD reference, so its CAD accuracy remains unknown
even when the nominal material exactly fills the mesh-relative targets.

This is a numerical preview fixture, not a print job. The tracks are sequential
5D preview data, not a synchronized three-head schedule. Material adhesion,
actual deposited shape, underlay strength and collision clearance are unverified.
The experimental print/export block remains enabled.

Ordinary test runs leave no persistent artifact. To reproduce the archive, run
the Release `libslic3r_tests` executable with filter `[NativeRoundTrip]` and set
`FORMORIX_TEST_ARTIFACT_DIR` to an explicit output directory. The test writes
`formorix-three-materials.3mf` there only after exercising the native roundtrip;
unset the environment variable afterwards. The committed fixture is an exact
copy of that output, renamed for its stable repository path. Archive bytes can
change with generated metadata; the tested geometry and profile invariants are
the reproducibility contract.
