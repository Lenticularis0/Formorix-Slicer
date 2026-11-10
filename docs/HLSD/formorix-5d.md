# Formorix Slicer: experimental 5D configuration

Formorix Slicer is a fork of OrcaSlicer. The existing C++ slicing engine,
wxWidgets application, profiles, resources, icons and AGPL license remain the
base of the application. `version.inc` and `libslic3r.h` provide the visible
application names. The `FormorixSlicer` application key separates user settings
from an installed OrcaSlicer; the translation catalog remains `OrcaSlicer`.
The About dialog retains upstream credits and identifies the fork.
G-code producer detection and configuration import accept both the Formorix
name and the original OrcaSlicer headers.
The plugin audit registry protects both Formorix and legacy Orca application
config filenames, including backup suffixes and case-insensitive spellings.
The application-update check points users to the fork instead of offering
upstream executables that would remove Formorix functionality. The profile
update path is unchanged.

## Architecture

- `src/OrcaSlicer.cpp` starts the GUI and implements the CLI.
- `src/libslic3r/Print.cpp`, `PrintObject.cpp` and `GCode.cpp` implement the
  conventional FFF pipeline. Their geometry and toolpath algorithms are unchanged.
- `PrintConfigDef` defines the settings, `PrintConfig` carries the printer-wide
  values into the engine, and `Preset::printer_options()` controls persistence.
- `src/slic3r/GUI/Tab.cpp` adds a Formorix page to the existing printer dialog.
  Standard option groups provide editing, undo, dirty tracking and preset saving.
- `src/libslic3r/Formorix5D.{hpp,cpp}` owns the experimental model, definitions,
  key list, validation and the common print-job prohibition.
- `resources/profiles/Formorix.json` registers the experimental vendor bundle.

## Data and coordinate conventions

Schema version 1 contains exactly three independent heads. Each has X/Y/Z
positions and minima/maxima in mm, A/B positions and minima/maxima in degrees,
and a unique logical extrusion channel (zero-based, 0 through 255). These are
configuration values, not live positions. A/B have no assigned rotation order,
pivot, inverse kinematics or firmware mapping. The 75-degree default is an
independent bound for each axis; it is not a guarantee of combined tool tilt.

Head X/Y use the provisional machine coordinates. Head Z describes the local
100 mm travel; the shared bed has its own Z position and 350 mm travel.
Printable height remains 350 mm: the two travel values are not added to claim
a 450 mm build volume. Collision and reachability validation are not implemented.

The initial envelope is 1600 x 350 x 350 mm. Target speed (500 mm/s), target
acceleration (10000 mm/s²), tilt and all travel bounds are editable. These
experimental targets do not modify planar process speeds or emit firmware
commands. Reducing an axis limit below its configured position makes the
configuration invalid; positions are not silently clamped.

Every experimental setting has a `formorix_5d_` prefix. Head values are scalar
keys, deliberately outside Orca's nozzle/extruder variant arrays, so changing
conventional nozzle counts cannot resize or merge the three head configurations.
The three logical extrusion channels are not Orca extruder indices. The standard
nozzle and Generic PLA in the experimental preset are explicitly provisional
placeholders, not hardware calibration or firmware support.

## Persistence and compatibility

All keys belong to the printer preset and to the full print configuration.
They use the existing JSON preset and 3MF project serializers. Old configurations
receive `formorix_5d_enabled = false` and schema version 1 from defaults.
No old key is renamed or given a different meaning. Unsupported schema versions
are rejected by the model reader. Any future schema change needs an explicit
migration before that version can be consumed.

The Formorix preset enables experimental mode by default. Other presets remain
in conventional mode. Switching modes invalidates cached output. While disabled,
experimental head settings do not affect slicing algorithms or motion commands;
they can still appear as metadata in the normal serialized configuration block.

## Output boundary

Experimental mode is configuration-only to the executable print pipeline.
The separate [internal preview](FormorixPreview.md) can generate nonplanar
geometry and TCP poses without passing them to that pipeline. `Print::validate` reports the reason,
and `Print::process`, both Print G-code export paths and `GCode::do_export`
reject it before writing an output file. This also protects CLI callers and
callers bypassing normal GUI validation.

The plater checks the edited printer before file export and send/print dialogs,
including previously sliced jobs. Project saving removes G-code, slice-info
and statistics flags, allowing editable geometry/configuration projects.
The 3MF writer additionally refuses an executable archive with experimental
configuration. Automatic backups retain their mesh references while omitting
G-code and G-code references, including any from an earlier conventional job.
There is no simultaneous-head scheduler, collision solver, Klipper integration
or 5D machine-command generator. The single-head preview does not lift these restrictions.

## Checks

`tests/libslic3r/test_formorix_5d.cpp` covers JSON and project round trips,
the vendor profile, invalid envelopes/channels/non-finite values, missing keys,
future schemas and executable-3MF rejection.
`tests/fff_print/test_formorix_5d.cpp` covers the slicing/export guards and
unchanged conventional commands when disabled head parameters are edited.
Run with the existing Catch2 suites, filtering by `[Formorix5D]` for focused checks.

Hardware limits, nozzle diameter, materials, rotation conventions and extrusion
mapping must be calibrated before any future executable 5D support is designed.
