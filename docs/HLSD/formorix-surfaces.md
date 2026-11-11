# Formorix surface editor

The **5D Surfaces / 5D-Oberflächen** tab stores surface selection and manufacturing
intent. Its explicit preview action passes the active region to the separate
[nonplanar preview module](FormorixPreview.md). Supports, head schedules and
machine commands are not generated. The experimental printer/export guards
described in [formorix-5d.md](formorix-5d.md) remain authoritative.

## GUI and selection

`MainFrame` adds a page sharing the existing `Plater`, like Prepare and Preview.
`FormorixSurfaceEditor` lazily creates two AUI side panels around that same
OpenGL canvas. Leaving the tab restores the ordinary sidebar. Native navigation,
object clipping and camera tools are reused; no virtual print heads are drawn.

`GLGizmoFormorixSurfaces` derives from Orca's `GLGizmoPainterBase`. It reuses
connected-face selection, subdivision-aware brush painting, erasing, raycasting
and `TriangleSelectorPatch` rendering. Region IDs use the existing 0–32 triangle
state codec (0 is unassigned), independently of filament, seam and support paint.
There are at most 32 project regions. Colors repeat after eight regions; names
and IDs distinguish them. An individual surface fragment belongs to one region.

The object selector chooses the instance to paint. Slope selection measures
transformed outward normals against global +Z. The edge-angle threshold limits
connected selection. A reference-edge click stores the nearest mesh-triangle
edge endpoints in volume-local coordinates as a direction reference; this is
a captured reference, not a live CAD edge constraint. The preview uses the
transformed reference direction only when it resolves to a unique volume.
The section control clips the original model relative to the current camera.
It does not depict a calculated inward shell or a measured wall thickness.

Regions support naming, multiselection, merging, copying settings, hiding and
removal. The last clicked region supplies settings and head assignment for
merge/copy. A merge remaps triangle states and requests continuous cross-surface
paths; it neither establishes adjacency nor proves a continuous extrusion path.
Single-head restriction defaults on. Cross-head transitions are stored by an
unordered region pair and remain unverified manufacturing wishes.

## Data ownership and persistence

`libslic3r/FormorixSurfaces` is a typed annotation model with bounded values and
schema version 3. Versions 1 and 2 migrate with an explicit binding to nozzle slot zero
of the current edited printer. Each head can instead reference another existing printer
preset and nozzle slot, independently of its material preset. Optional resolved process
values are cached evidence, never authoritative calculation inputs. The
[profile resolver](FormorixProfiles.md) invalidates them on source changes and recomputes
from current Orca configuration before a hybrid calculation. No second profile manager is added.
`Model::formorix_surfaces` retains its JSON representation.
`ModelVolume::formorix_facets` owns the triangle mask; `formorix_topology` records
a hash of vertex count and ordered triangle connectivity. Ordinary transforms
preserve ownership. A topology mismatch prevents rendering/reusing old masks;
replacement geometry must be selected again. Native operations that explicitly
reset all facet annotations also reset these annotations.

The existing 3MF writer stores `Metadata/formorix_surfaces.json`, per-triangle
`formorix_surface_v1` attributes, and per-volume
`formorix_surface_topology_v1` metadata. Mesh deduplication includes mask and
topology identity. The importer preserves unsupported/malformed JSON verbatim
and disables editing; data larger than 4 MiB is rejected. Unknown schema data
and inactive topology-mismatched masks survive a project round trip.

These fields participate in native model copy and cereal snapshots for undo.
Project save commits a focused numeric/name editor first. Settings changes
mark the project dirty and enqueue its normal backup. Paint updates use the
native object mesh backup. Importing a 3MF *as geometry* drops incoming Formorix
region masks instead of attaching foreign region IDs to the current project;
open it as a project to retain its settings. The source file is unaffected.
Other slicers may discard these extension fields when saving; use Formorix for
annotation-preserving project edits.

## Profiles and manufacturing wishes

Collapsible groups cover shell depth/count/full wall, nozzle deviation,
underlay, surface finish, region transitions, head assignment and three-head
operation. Depth is specified along the local normal and preserving nominal
dimensions defaults on. Future calculations are labelled as pending.

The support group instantiates Orca's `ConfigOptionsGroup` with the existing
process keys. Its edit buffer synchronizes with the edited process preset and
writes through `Tab::apply_preset_config_value`. Thus support type, spacing,
contacts, interface, patterns, material slots and Infill first have one
authoritative process configuration and also affect conventional printing.
The separate per-region support wishes do not create support geometry.

Heads reference existing Orca filament preset names and allow multiple tasks.
Material files are not duplicated into a second profile system. A missing
preset remains an unresolved reference; it is never silently substituted.
Speed, line width, flow, temperature and cooling remain inherited. High-speed
eligibility checks enabled heads, compatible PETG presets and overlapping
temperature ranges only. Even a positive result requires later calibration,
kinematic, bonding, collision and scheduling checks.

Neither `Print` nor `GCode` reads these annotations. With the experimental
printer mode disabled, conventional planar output is unaffected by region
painting and wishes. Enabling experimental mode retains all existing rejection
paths. The surface tab offers an internal preview action, with no executable export.

## Localization and tests

English source messages use Orca's translation wrappers; German follows the
requested Formorix terms. The fork-specific glossary additions are **5D
Surfaces → 5D-Oberflächen**, **Multi-head operation → Mehrkopf-Betrieb**, and
**Interwoven transition → Verwobener Übergang**. The request's terms Infill and
5D-Ironing are deliberately retained in their new labels.

`[FormorixSurfaces]` tests cover all setting fields, schema validation, merging,
removal, split-triangle brush masks, opaque future data, topology mismatch,
model cloning, actual 3MF I/O, conventional G-code equivalence and experimental
export rejection. GUI interactions require a live OpenGL window; see the
acceptance report for the platform and interactions actually tested.
