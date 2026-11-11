# Formorix nonplanar preview

`libslic3r/FormorixPreview` is a bounded, cancellable geometry and time module. It consumes a painted patch, its closed source solid and `FormorixSurfaces::Settings`. It does not participate in `Print`, `GCode`, networking, machine configuration serialization or executable export. Existing Formorix export guards remain independent of successful preview generation.

## Geometry

Extraction uses Orca's subdivision-aware triangle painting and the selected model instance's world transform. Mirrored transforms reverse winding. The planner welds referenced vertices, validates an oriented manifold solid and requires a connected topological disk for the patch. A common tangent-plane projection must have positive triangle orientation, a simple boundary and one scan interval per raster row. This deliberately limits the solver to smooth graph-like patches; it is not a general surface parameterization.

Area-weighted vertex normals provide a continuous normal field. A projected boustrophedon raster uses conservative row spacing from the minimum normal/projection cosine. Consecutive rows join along their common boundary. Projection-grid queries insert all crossed triangle edges into the path, so the outer polyline follows the source mesh instead of shortcutting across concave facets. These extra vertices do not interrupt extrusion. Original region IDs and triangle boundaries are not planning boundaries.

Requested layer depths are measured inward along the interpolated normal; the last TCP layer has zero offset. Triangle orientation and projected boundary tests reject folded offset surfaces. Normal rays into the closed source mesh check available wall thickness at mesh vertices and sampled paths. The local BVH traversal handles zero ray components without NaN-producing inverse-direction multiplication. This does not alter Orca's general raycaster. Full-wall mode requires nearly uniform measured thickness. Checks are conservative prototype checks, not a certified global self-intersection or machinability proof.

The core visualization joins the unselected remainder to the innermost boundary with side strips. It is an illustrative boundary mesh, not a Boolean remesh or a new planar slicing input. All-or-nothing failure clears paths and time data while retaining the selected mesh for diagnostic coloring. Resource bounds cap source faces, selected faces, boundary vertices, layers, raster rows and motion segments.

## TCP and time

Axes point from TCP into the nozzle. They follow the local normal unless an explicitly allowed deviation is necessary to respect the configured tilt. Vertical permission and the at-most-15-degree limit are checked; machine inverse kinematics and collisions are absent. Layer-to-layer travel segments are straight, unverified preview connections.

`Timeline` owns three independently sorted, non-overlapping tracks. A `Motion` contains head ID, absolute start/end seconds, two poses, movement kind, extrusion flag, layer and region. Tracks may overlap across heads. `sample` performs a binary search and linear position/spherical axis interpolation; gaps and completed tracks wait, empty tracks are unassigned. Time estimates use constant profile speed and ignore acceleration and unplanned operations. The current generator writes exactly one track; no scheduling is performed.

`HeadState::pose_known` distinguishes a calculated preview pose from an unassigned head. An empty track has no known pose: its default `Pose` storage is not a machine position and must never produce a tip, axis or coordinate label. There are no synthetic parking positions. A nonempty valid track supplies its first planned pose before its start, its previous endpoint during a gap, and its final endpoint after completion; these are waiting preview states, not measured machine positions. All three head status labels remain available independently of whether a spatial marker can be shown.

Motions and sampled states may also carry an independent `FormorixProcess::Profile` with material/nozzle IDs and deposition parameters. `process_motion` creates synthetic or externally supplied single segments using that profile's own speed and volumetric ceiling. Legacy GUI preview motions leave this field absent; their TCP paths do not claim material-envelope precision. The separate [geometric foundations](FormorixFoundations.md) must be explicitly integrated and validated before such a claim.

## GUI integration and lifetime

The existing 5D editor submits a copied input to Orca's UI job worker. Finalization applies the result only if the editor is alive and the model/settings fingerprint still matches. Fingerprints include painting timestamps, vertex/index content, transformations and profiles. Playback data is a disposable cache; native 3MF annotations remain the persisted source of truth.

The existing Plater canvas and Preview tab host the visualization. A bottom dock contains a horizontal time slider, transport controls and layer/shell/core filters. The render pass reads the three time tracks directly, groups extrusion geometry by head/layer and uses per-group end-time binary searches for progress coloring. Future schedulers can therefore provide the same tracks without replacing the renderer. Small tips and axes replace large machine models. Timer callbacks run on the GUI thread, use a monotonic clock and pause when hidden.

The clock explicitly identifies the estimate as the 5D part only. Persistent labels exclude normal construction, supports and waiting time, and mark layer-change travel as collision-unchecked. The traveling head status repeats that qualification; the renderer does not certify safe travel.

Hybrid material display consumes `FormorixMaterial::Result` directly. Accepted deposited, missing, outside, core-overlap and cross-material-overlap meshes require a completed audit; failures expose only explicitly unverified raw-cell wireframes. The all-layer material bodies remain static as the time and layer controls select path playback. Empty or unavailable datasets disable their display controls. Same-material duplicate volume has no separate spatial mesh and remains a numerical report item.

Only the GPU copy is converted to Float. Finite-index and nondegenerate-triangle checks also cover accepted Double bodies whose very thin faces collapse at display precision. Those omissions never alter the audit mesh or establish zero missing material. Availability is cached when the immutable result arrives, not recomputed during every timer tick. Maximum, mean and RMS are labeled as sampled statistics, unbounded results remain unknown, and the regional 0.01-mm status uses the existing complete `Accuracy::verified()` gate.

`FormorixHelpTopic` maps controls to one central table of documentation URLs. The targets are pinned to a published commit retained in the merged history, so deleting a development branch cannot invalidate help. A documentation update can centrally change this revision, or switch to a Wiki. The ordinary Orca preview remains reachable explicitly, and ordinary projects never enter this path unless the user generates a Formorix preview.
