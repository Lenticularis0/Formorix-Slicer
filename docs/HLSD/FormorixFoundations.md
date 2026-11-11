# Formorix geometry and process foundations

These modules are independent of `Print`, `GCode` and the surface coverage planner. They describe nominal geometry and evidence, not permission to move a machine. All lengths are millimetres. No executable 5D export is introduced.

## Solid partition

`FormorixGeometry::Mesh` stores double vertices. Orca's `indexed_triangle_set` stores float vertices, and the existing `MeshBoolean::cgal` conversion rounds to float; that interface is intentionally not used for precision-critical output. `FormorixGeometryCGAL.cpp` is compiled in the existing `libslic3r_cgal` target with CGAL's floating-point settings. It uses `Exact_predicates_exact_constructions_kernel` for constructions and Boolean operations, starting from the exact binary values of the supplied doubles. This improves robustness of the mesh computation, not the fidelity of the imported mesh to unknown CAD.

A shell request has an oriented surface, outward normal and positive depth per vertex, and a region ID. Sweeping those vertices inward creates outer/inner sheets and side faces at open boundaries, including holes. Normals and depths on shared vertices are shared data. Face inversion, degeneracy, invalid indices, self-intersections, non-manifold topology and inconsistent orientation are rejected. Closed circumferential surfaces do not require a height field. This is a piecewise-linear normal sweep, not an exact offset of an arbitrary smooth CAD surface.

Each cutter is intersected with the remaining solid while its complement is computed in the same corefinement operation. Nonzero cutter volume outside the remaining solid is rejected: this includes insufficient wall thickness and overlapping region assignments. It is never silently clipped or assigned to whichever material happened to come first. The final solids are mutually corefined and shared triangles become explicit interfaces. Region zero identifies the core. Different depths can meet at a step; no smoothing or blending of that step is implied. Exact volume conservation is required, and all materialized double solids are checked again. The exact-to-double coordinate displacement is recorded separately.

Before materialization, the union preserves the supplied solid exactly and the partition has disjoint interiors. All output parts and interfaces use one binary grid of 2^-30 mm (about 9.31e-10 mm). This avoids inconsistent double rounding of near-coincident Boolean vertices. The largest exact-to-grid displacement is bounded using CGAL intervals. Zero-area faces are removed topologically, without smoothing or introducing new coordinates; invalid resulting topology is rejected. Converted solids are checked for closure, orientation, self-intersections and mutual positive-volume overlap. `volume_residual` reports their total volume minus the input volume, including grid rounding. Interfaces are oriented outwards from their first region. Output is therefore subject to the measured rounding bound, not a claim of bit-exact original dimensions.

CAD error and offset approximation error remain separate questions. Full-wall consumption is possible when a valid cutter exactly consumes a solid. General variable-thickness full-wall offsetting, automatic patch extraction from the GUI, arbitrary self-intersection repair and generation of load-bearing supports are not part of this module. A failed request returns no partial accepted parts. Patches must match the source surface: a slightly displaced coplanar patch is not silently projected back onto it.

CGAL's existing LSCM unwrap targets disk-like patches; isotropic remeshing improves triangle distribution but cannot recover a lost CAD surface. Neither is a substitute for CAD error control. See the [CGAL Boolean documentation](https://doc.cgal.org/latest/Polygon_mesh_processing/group__PMP__corefinement__grp.html) and [parameterization documentation](https://doc.cgal.org/latest/Surface_mesh_parameterization/index.html).

## Accuracy evidence

`measure` reports directed mesh-to-analytic-surface distances, using an actual signed Euclidean distance, not a polynomial implicit residual. Plane, sphere and cylinder references supply analytic distance and conservative Hessian bounds. Each triangle is adaptively subdivided until the gap between sampled error and the upper bound is below the requested slack. The bound uses a Taylor remainder, or a 1-Lipschitz distance bound when the Hessian is unavailable. Sampled maximum includes vertices and centroids; mean and RMS are area-weighted centroid quadrature. They are measurements, not certified integrals. The upper bound covers triangle interiors. A resource/depth limit marks the measurement insufficiently verified.

This is not a symmetric Hausdorff/coverage certificate: missing CAD regions, open edges and wrong surface correspondence require additional checks. A raw STL/3MF without a CAD reference carries unknown import error. Mesh refinement alone cannot reduce that error. Double arithmetic is used throughout; distance bounds are analytical bounds evaluated numerically, not a general interval-arithmetic proof of every floating-point operation.

`discretize` uses a supplied bound M on the complete parameter interval's second derivative. A chord has deviation at most M Δt² / 8 from the curve at the same parameter. It recursively refines intervals, preserves order and reports exhaustion. It does not infer a proof from a few midpoint samples. The NACA test uses x = c t², removing the square-root leading-edge singularity before deriving the bound.

`AccuracyBudget` separates import/CAD, shell, center-path interpolation and nominal deposition errors. All four upper bounds must be finite, nonnegative and sum to the target tolerance. Unknown terms default to infinity. Even a passing budget says nothing about real printer accuracy, adhesion, strength, temperature history or collisions.

## Nominal deposition

`FormorixProcess::Profile` owns a material ID and nozzle-profile ID, diameter, actual line width, layer height, temperature, volumetric ceiling, speed, flow ratio, three shrink-compensation ratios and width/height calibration factors. Every head and motion can carry a different profile. The Orca adapter resolves explicit nozzle and filament indices; it does not default to nozzle zero. Material preset names remain the existing `FormorixSurfaces::Head::material_preset` reference. Optional resolved process values are persisted in surface schema 2. Schema 1 migrates without inventing process/material data; unknown future schemas retain the existing opaque preservation behavior.

The nominal cross-section is an ellipse normal to the travel tangent. Its half-axes are a = corrected line width / 2 and b = corrected layer height / 2. The height axis is the nozzle axis projected into that plane, so cross-track tilt changes the deposited ellipse. Longitudinal tilt deformation, die swell, wetting and rheology are not simulated. Diameter constrains the allowed layer height but is not substituted for the actual line width.

With unit cross-section axes u and v, Q = a² uuᵀ + b² vvᵀ. Its support vector in direction n is Qn / sqrt(nᵀQn). The bead center is CAD contact minus that vector. Thus the nominal outer contact, not the center or TCP, lies on the CAD contact. TCP is the contact plus the configured axial contact-to-TCP offset. `compensate_curve` applies this to a supplied contact curve and adaptively discretizes the compensated center; it is not a general surface-filling algorithm. Its derivative bound must apply to that compensated center.

The planar scallop rule limits pitch s by b(1 − sqrt(1 − (s / (2a))²)) ≤ ε. It describes the upper envelope of equal adjacent nominal beads. Area is πab, and nominal speed is limited by Qmax / (area × flow ratio).

`FormorixProcess::planar_coverage`, declared in `FormorixDeposition.hpp`, explicitly accounts for overlapping material. For an infinite aligned equal-bead row, one pitch cell has union area 2ab(asin(u) + u sqrt(1−u²)), where u = s/(2a). It compares this with supplied area πab × flow ratio, reports duplicate/deficient volume per unit path length, and the ratio that would balance nominal union volume. This is bookkeeping, not an automatically applied flow setting: reducing flow may change the deposited shape. The integral handles multiple overlapping neighbours, not just pairwise lenses. Pitch greater than the width is rejected as a lateral gap.

These cross-sections do not constitute a volume-conserving fluid simulation. Intentional overlap and actual material redistribution need calibration. Different widths have a nominal lateral contact-spacing helper, but it does not establish full-volume filling or material adhesion. The periodic coverage integral does not apply to mixed-width, curved or tilted boundaries. Curvature, tilt and transverse coverage must be measured separately; the contact-point identity alone is not a 0.01-mm surface certificate.

Shrinkage compensation is metadata; it is not automatically applied as three conflicting global scales to one shared CAD model. Material-pair compatibility has its own Unknown/Compatible/Incompatible state. Geometrically coincident materials can still be incompatible.

Resolved process data are snapshots. Changing the assigned Orca material name clears them; changing values inside an existing Orca preset does not automatically resolve a fresh snapshot yet. A future hybrid consumer must resolve/revalidate these values against the active configuration. The legacy GUI preview does not consume the new snapshots.

## Complex surfaces and continuity

The atlas is deliberately an exact per-triangle barycentric atlas. It handles manifold holes, vertical faces, overhangs and circumferential profiles without a global projection or an unwrap-distortion assumption. Shared topological edges are portals. Larger LSCM/ARAP charts could be introduced later while preserving these portals and source identities. This atlas does not yet choose a good coverage direction or optimally group triangles.

`join_across_charts` checks that every piece stays in its convex triangle, meets its neighbour at the same shared edge and uses the same head/material. It emits a single point sequence without a duplicate seam endpoint. No restart is attached to a chart boundary. Disconnected pieces, mismatched endpoints, and different physical heads/materials cannot be labelled one uninterrupted extrusion. Original region assignment remains separate from computational chart identity.

## Manufacturing dependencies and underlay

Operations distinguish core, dense transition, solid, breakaway, soluble, full wall, surface and reinforcement work. Directed prerequisite edges are checked for missing IDs, duplicates and cycles. Completion queries check the transitive prerequisites. They do not assign times, choose heads or create a schedule.

`UnderlayEvidence` requires a conservative maximum gap over the complete bead footprint, completed underlay and explicit material compatibility. Unknown coverage or compatibility is not accepted as supported. Geometry alone cannot prove strength. Ordinary infill can serve as underlay only if the contact footprint and span are justified; an infill percentage alone is insufficient. Infill-first is a sequencing constraint, not proof of coverage. Dense transitions and solid regions can improve footprint coverage. Breakaway and soluble supports additionally need interface-gap and material checks. Full-wall deposition still needs a substrate or a justified bridge. No evidence is automatically inferred from GUI wishes, and no supports or manual timing controls are added.

## Machine envelopes

Forward transforms describe stage XYZ, Z yaw and tilt, a configurable rotation center, pivot-to-TCP displacement and the common bed-Z displacement. Per-head XYZ/tilt envelopes and the bed range are checked. Cone and shaft bounds use conservative capsules; the housing uses a configurable circumscribed sphere. Dimensions are editable in the model and explicitly marked as placeholders. These are geometric envelopes, not graphical toolheads.

Bounding boxes identify possible head/head and head/part interactions. Separation of boxes for these supplied static shapes is not clearance for a swept motion, unknown fixtures or incorrect placeholder dimensions. There is no narrow-phase collision solver, inverse kinematics or executable motion approval.

`FormorixPreview::Motion` and `HeadState` optionally retain the complete process profile. Synthetic independently timed motions use each profile's own effective speed and sample on the existing three tracks. Legacy preview motions keep an empty process field; their original preview behavior is unchanged. There is no scheduler or automatic three-head work allocation.
