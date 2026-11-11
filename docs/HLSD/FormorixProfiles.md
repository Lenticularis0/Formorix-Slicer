# Formorix profile resolution

Each Formorix head binds independently to an Orca material preset and to a nozzle slot
in an Orca printer preset. Empty preset names explicitly follow the currently edited
material/printer. Slot indices are zero-based in project data. These are references to
existing profiles, not a second material or printer management system.

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
width (falling back to generic width and then 1.05 times diameter), current process
layer height and outer-wall speed, temperature, flow, volumetric ceiling and shrink
compensation. A supplied positive surface layer-height override is explicit. Actual
Orca core extrusion entities can carry different role-specific widths and heights;
they must not be replaced by this generic surface profile.

The invalidation signature contains serialized source configurations, preset names and
IDs, active machine/project settings and the nozzle slot. It is the exact serialized
content, not a name, timestamp or short hash. This catches same-name edits, deleted
sources and printer/material changes. A fresh signature cache invalidates snapshots
loaded from 3MF. `refresh` clears the snapshot before trying resolution, so failure
cannot preserve a previously usable profile. Every new hybrid calculation resolves
again even if a cache appears current.

Surface schema 3 persists the nozzle binding. Versions 1 and 2 migrate to the explicit
current-printer/slot-zero binding; their old snapshots do not establish source freshness.
Unknown future schemas retain the existing opaque preservation behavior.

The `[FormorixProfiles]` tests cover distinct nozzles/materials, unsaved edited sources,
explicit nonzero slots, volume-type variants, unchanged-name parameter changes, invalid sources, snapshot
discarding and schema migration. Native 3MF tests additionally retain the bindings.
