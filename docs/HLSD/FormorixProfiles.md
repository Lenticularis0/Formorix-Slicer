# Formorix profile resolution

Each Formorix head binds independently to an Orca material preset, an Orca process
preset and a nozzle slot in an Orca printer preset. Empty preset names explicitly
follow the currently edited material/process/printer. Slot indices are zero-based in
project data. These are references to existing profiles, not a second profile manager.

`source` copies current source presets and configuration on the GUI thread. A named
preset equal to the edited preset uses the edited values, including unsaved changes.
Worker code receives copied data and does not access `PresetBundle`.

`resolve` invokes the existing `PresetBundle::construct_full_config` composer. Only
classified extruder options and their variant layout are taken from the assigned nozzle-source printer. The
active machine's bed, global settings and experimental flag remain in force. The
selected nozzle slot is checked before and after composition; missing or changed
layouts are rejected instead of falling back to slot zero. The resulting full config
retains Orca material/process settings beyond the small deposition snapshot, including
cooling and support options. One selected material preset produces material index zero.
The nozzle volume type selects the corresponding material variant (including High Flow).
A missing material variant is an error. Mixed physical-nozzle inventories within one
Orca extruder, such as its Hybrid volume type, are currently rejected because a single
Formorix extruder slot does not identify one concrete nozzle in that inventory. This
does not restrict three separate heads with independent nozzle presets and diameters.

The nominal surface profile resolves the selected diameter, nozzle-relative outer-wall
width (falling back to generic width and then Orca's native external-perimeter auto
width), assigned process layer height and outer-wall speed, temperature, flow,
volumetric ceiling and shrink compensation. `Flow::new_from_config_width` supplies the
native automatic-width semantics. Explicit millimetre and percentage dimensions stay
in double precision; only the native automatic result inherits Orca's float precision.
A supplied positive surface layer-height override remains explicit and takes precedence
over the assigned process. Actual Orca core extrusion entities can carry different
role-specific widths and heights; they must not be replaced by this surface profile.

`Profile::flow_ratio` remains the selected filament factor. `print_flow_ratio` and
`surface_flow_ratio` retain separate process factors. The latter is the outer-wall
factor only when Orca's `set_other_flow_ratios` is enabled, otherwise one. Bed-first
and top-infill factors do not apply to this surface role. Nominal surface supply uses
their product without writing it back to Orca's filament configuration. Native core
paths resolve their own role factors, rather than inheriting the surface role.

Optional per-head `WidthLimits {minimum_mm, maximum_mm}` are explicit requested-width
declarations in mm. Their absence allows only the resolved nominal width; Arachne's
minimum bead and transition heuristics are not interpreted as a general permission
to vary 5D widths. The GUI initially declares a fixed interval at the current nominal
width; users must explicitly expand it. Both endpoints, the nominal width and every
local request are validated. The representable 0.01–10 mm input domain is a numerical
configuration bound, not a physical process certificate. Corrected stadium width must
also be at least its corrected height. Unfillable residual strips remain an error.

`ribbon_section(profile, requested_width)` evaluates the actual local stadium area
`h * (w - h * (1 - pi/4))` and unchanged effective flow, with corrected `w` and `h`.
Its compacted width is supplied area divided by corrected height. The corresponding
`ribbon_speed` uses this local supplied area for the volumetric limit. These helpers
do not modify the source profile or introduce any material calibration.

The invalidation signature contains serialized source configurations, preset names and
IDs, active machine/project settings, nozzle slot and declared local width limits. It is the exact serialized
content, not a name, timestamp or short hash. This catches same-name edits, deleted
sources and printer/material changes. A fresh signature cache invalidates snapshots
loaded from 3MF. `refresh` clears the snapshot before trying resolution, so failure
cannot preserve a previously usable profile. Every new hybrid calculation resolves
again even if a cache appears current.

Surface schema 4 persists the process binding and optional declared width limits in
addition to schema 3's nozzle binding. Versions 1–3 migrate to the current edited
process and absent width limits; existing explicit nozzle bindings remain intact,
with missing legacy nozzle bindings defaulting to current printer/slot zero. Old
snapshots do not establish source freshness. Unknown future schemas retain the
existing opaque preservation behavior.

The `[FormorixProfiles]` tests cover distinct nozzles/materials, unsaved edited sources,
explicit nonzero slots, volume-type variants, unchanged-name parameter changes, invalid sources, snapshot
discarding, schema migration, native automatic/percentage width resolution and separate
flow factors. Native 3MF tests additionally retain the bindings, factors and limits.
