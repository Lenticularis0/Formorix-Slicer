# Formorix hybrid preview

The hybrid preview partitions a copy of one selected model instance into a conventional core and marked nonplanar shells. It uses Orca's existing `Print` pipeline for the core and Formorix's surface planner for the shells. It does not modify the source mesh, generate machine instructions, or provide a synchronized hybrid print schedule.

## Data flow and ownership

`FormorixSurfaceEditor` copies the selected instance, painted facet assignments and current profile sources on the GUI thread. A cancellable job owns these inputs and the resulting geometry. The completion callback checks both the editor lifetime and a fingerprint of geometry, paint, transforms, settings and profile contents before publishing a result. A changed source invalidates the preview.

The principal modules are:

| Module | Responsibility |
| --- | --- |
| `FormorixProfiles` | Resolve each head's current Orca material, nozzle and process settings; detect stale saved process snapshots. |
| `FormorixHybrid` | Extract marked surfaces, partition shell/core volumes, compensate nominal material placement, generate surface paths, and audit coverage, volume and underlay. |
| `FormorixCorePreview` | Process the actual partitioned core through native Orca slicing and copy its extrusion geometry into source coordinates. |

`FormorixGeometry` supplies double-precision meshes, reference measurements and CGAL Boolean partitioning. `FormorixProcess` supplies the nominal extrusion cross-section and placement model. `FormorixPreview` supplies surface raster seeds and three independent time-indexed head tracks. The renderer consumes immutable shared results; its float/OpenGL geometry is display data, not the calculation reference.

## Profile resolution

Each of the three heads has an independent material preset, optional nozzle-source preset, explicit physical nozzle slot, and resolved process profile. Assigning a nozzle-source preset retains the active machine's bed and other global machine settings. Orca's existing preset composer resolves the material/extruder variant. Ambiguous mixed nozzle inventories and missing material variants are rejected.

Signatures include serialized source contents, rather than only preset names. Editing a preset in place therefore invalidates a previously saved snapshot. Loaded snapshots are also invalidated before use. Workers receive copied resolved configurations and never access a live `PresetBundle`.

The native core adapter projects the selected physical nozzle and material slots into an isolated single-nozzle configuration. It projects process variants and normal/silent machine-limit pairs as well as nozzle options. It does not flatten unrelated vectors such as bed coordinates. Returned extrusion paths retain head and material identity; their actual width, height and `mm3_per_mm` come from Orca's extrusion entities. Speeds are nominal role values subject to the configured volumetric limit, without acceleration or cooling-time simulation.

Nonneutral XY or Z shrink compensation is rejected for shell, core and support heads. Orca would otherwise scale the core independently of the CAD-referenced shell. The adapter neither silently disables this compensation nor claims that the two geometries still fit; a shared compensation transform is required to support it.

## Shell, core and nominal deposition

Marked facets are transformed into the selected instance's coordinates and compacted into double-precision surfaces. Area-weighted normals are shared at adjacent marked vertices. Shell depth comes from layer count, requested depth, or a supported full-wall thickness; the final partial layer can be thinner. Sampled inward intersections reject insufficient thickness and unsupported variable full-wall termination.

Closed normal-offset shell candidates are clipped to the original solid and subtracted using the existing exact Boolean machinery. The result retains shell volumes, core volume and explicit shared interfaces. Invalid solids, overlapping shell assignments, collapsed offsets and failed containment checks produce errors. Boolean exactness relative to the input mesh does not establish accuracy relative to an unknown CAD surface.

Surface raster seeds are refined according to midpoint displacement, orientation change and segment length. Material centers and desired nozzle TCP positions are distinct. `FormorixProcess::place_bead` positions a parametric nominal elliptic cross-section against the target surface, using each head's line width, layer height and orientation. Adaptive refinement on a curved mesh is a sampling procedure, not a proven global error bound.

Coverage tests examine the marked area, including points between contact curves. Material-volume plausibility compares integrated nominal extrusion volume with the Boolean shell volume and checks overlapping planar cross-sections. A failed volume balance is reported; the implementation does not invent an automatic flow correction. Material adhesion, cooling, shrinkage calibration and the actual deposited shape require separate evidence.

## Native core slicing and export isolation

`FormorixCorePreview` constructs a separate `Model` containing the computed core. It translates the mesh into a local frame before conversion to Orca's float mesh, measures the maximum vertex displacement and volume residual, and transforms copied extrusion paths back to source coordinates. Path Z denotes the top of the conventional layer.

The adapter calls `Print::process()` and traverses native perimeter, fill and support extrusion entities. It never calls a G-code exporter or generates an intermediate G-code file. A separately assigned support head receives its own native Orca pass, from which only support paths are retained. This provides independent nozzle/material geometry; it is not a combined support/core timeline.

The temporary `Print` is constructed with the immutable `Purpose::HybridPreview`. This purpose survives `clear()` and configuration replacement. `Print::require_executable()` blocks ordinary, previous-file and cached print exports, and `GCode::do_export()` checks the same guard before opening its output. Standard `Print` objects retain the existing experimental-mode export protection. The internal adapter never disables the experimental configuration flag.

Only export-specific E-addressing macro validation is omitted for this immutable purpose: a geometric preview has no G-code stream requiring a `G92 E0` reset. Geometry and process validation still runs, and ordinary `Print` continues to reject missing or incompatible reset macros. The adapter does not insert synthetic G-code into the user's configuration.

## Underlay and shared display

Underlay analysis receives actual native extrusion segments, widths and heights. It tests inward interface samples against nominal conventional bead sweeps, respecting the surface's infill/support choices. Missing contact is marked. Sampled contact cannot establish complete footprint coverage, material bonding, mechanical strength or a valid manufacturing order.

The shared preview can display the original solid, core, native core/support paths, shell solids, compensated 5D paths, interfaces, missing underlay and unverified samples independently. The timeline contains the planned 5D portion only. Conventional paths are static geometric evidence; they are not shown as synchronized activity. Three tip markers consume the existing independent head tracks, while inter-layer travel remains explicitly collision-unchecked.

## Accuracy contract and limits

The accuracy record separates CAD import, shell construction, float/Orca conversion, path interpolation and nominal material placement. An unknown bound is infinite. A 0.01-mm verification additionally requires whole-surface coverage and plausible material volume. Maximum, mean and RMS sampled errors are evidence, not substitutes for a maximum-error bound. No physical printing accuracy is promised.

The adapter's coordinate-grid allowance covers input, centering and output quantization only. It does not bound every Orca perimeter simplification, offset or conventional deposited surface. Mesh conversion can be measured even when the original CAD reference is unavailable; a small conversion error alone never certifies the complete hybrid result.

The prototype supports connected patches that the surface raster planner can parameterize, with valid clipped offset solids. It does not provide universal wrapped-surface planning, automatic topology repair, arbitrary variable full-wall termination, or a general solution for overlapping region shells. Extraction rejects negative volumes, modifiers, conventional painting, object/part process overrides and variable layer-height/range settings that cannot be transferred to the isolated core. Default first-extruder assignments and Formorix's own settings are permitted. Belt, spiral, sloped and Z-contoured conventional extrusion modes are rejected by the hybrid adapter; their normal Orca workflows remain separate.

There is no multihead scheduler, collision clearance, inverse machine kinematics, automatic hybrid support generation, or executable hybrid/5D export.

## Tests

`tests/libslic3r/test_formorix_hybrid.cpp` covers geometry, nominal placement, coverage and underlay behavior. `test_formorix_profiles.cpp` covers source invalidation and per-head resolution. `tests/fff_print/test_formorix_hybrid_core.cpp` verifies native wall/infill/support extraction, independent nozzle profiles, physical-slot selection, restored coordinates, shrink rejection, invalid/canceled requests and immutable export guards. Its integration cases pass a marked analytic shell through partitioning, native core slicing and the actual-path underlay audit, and use real paint from a saved curved-surface project to slice its computed core. Both retain the original source geometry.
