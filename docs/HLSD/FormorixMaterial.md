# Nominal spatial material evidence

`FormorixMaterial` represents the material of actual extruding moves as closed, double-precision cells. It is independent of `Print`, the GUI and machine execution. It implements the explicitly nominal, uncalibrated `CoalescedOrcaStadiumRibbon/v1` geometry; it does not describe measured melt behaviour.

## Inputs and geometry

A `SweepRun` is one uninterrupted extrusion with a head, material, region and layer identity. Its stations carry material-center positions, nozzle axes, local compacted widths and normal heights. TCP positions are not material centers. Each segment also carries its independently computed supplied volume. Travel, region changes and profile changes end a run; the module never invents extrusion between runs.

Adjacent segments share a miter cross-section and its triangulation. Ends have flat caps. The longitudinal cell faces are piecewise linear triangles between the shared rectangular sections. This is the complete nominal definition between stations, not a claim about a rotating rigid nozzle or a physical curved strand. Collapsed segments, excessive or reversing miters, folded cells and invalid solids cannot produce accepted volume evidence.

The planner can supply explicit lower/upper section centers from a shared cumulative layer-boundary table. These input endpoints avoid independently reconstructed layer interfaces; they are checked against the nominal station before any collinear simplification. The audit never snaps them. Each uninterrupted run is united in path order before regional/material grouping, so unrelated pieces do not create an artificial nonmanifold intermediate union.

Target shells and conventional core are separate inputs. A material body is never clipped to its target before validation: doing so would conceal over-deposition. Every layer contributes cells to the same audit, including hidden inner layers. The input model is never changed.

## Volume and distance evidence

The CGAL backend uses exact predicates and constructions on supplied binary coordinates. It computes material unions and differences against the target shells, as well as intersections with the core and other materials. Missing material, outside material and duplicate occupation are distinct quantities. Duplicate volume is the sum of cell volumes minus their union, so triple occupation is not incorrectly reduced to a pairwise-intersection count. Supplied material minus geometric cell volume is a separate ledger; geometrically overlapping material is not an excuse to alter Flow.

The raw binary input coordinates define the exact cells: there is no pre-union snapping that could erase an interior gap. Only the output vertices are converted back to double precision, with a conservative displacement bound and a separate volume residual. Output conversion retains face correspondence and rejects invalid display solids instead of repairing their topology. Exact volume results and rounded display meshes remain distinguishable; the original mesh is unchanged. A viewer consumes these meshes rather than substituting an unrelated decorative sweep.

Two directed boundary-distance calculations form a two-sided mesh-distance bound. Each adaptive source triangle is bounded over its entire interior, using distance to a fixed convex target triangle or the distance function's Lipschitz bound. Vertex and centroid samples provide maximum, mean and RMS diagnostics; their statistics are not the conservative upper bound. Spatial acceleration and explicit station, face, Boolean-operation, distance-leaf and distance-query budgets limit the work. They are not a hard RAM or wall-clock guarantee. Exhaustion yields incomplete evidence, not a successful certificate. Volume and supply ledgers accumulate eager exact rationals to avoid deep recursive lazy-expression chains on large paths.

The full solid boundary includes inner interfaces and boundaries of cavities. The additional `nominal_outer_distance` compares the outer-layer roof sheets with the marked surface; those sheets are not automatically the exterior of the material union. Agreement of only the visible outer layer is insufficient. A bound relative to the imported triangulated target does not establish a CAD bound when that reference is unknown.

For small exactly coplanar sheets, equality of two exact polygon unions proves zero distance without depending on their triangulation. Both set differences must be empty, including holes. The shortcut has an explicit arrangement-work bound; all other cases use the spatial distance procedure. A topology-changing fallback in shell materialization prevents a global accuracy certificate until its original-to-output surface bound is established.

## Integration and failure contract

`FormorixHybrid` creates runs from the actual extrusion mask and the same per-segment supply used by its timeline. Its accuracy decision requires the completed spatial audit in addition to the import, shell, conversion and interpolation budget. Each region's material ledger is checked independently; deficits and excesses in different materials cannot cancel. Outside material, core intrusion and duplicate occupation remain explicit barriers to a positive result.

Failure clears accepted evidence. `Status::Complete` means the Boolean analysis completed; distance evidence separately requires `bounded=true`. An exhausted distance budget retains an infinite upper bound and cannot certify accuracy. Untrimmed cell geometry may remain as diagnostic geometry only; it is a collection of closed cells, not a claimed union or a valid printable solid. Canceled calculations are discarded by the hybrid job. CGAL visitor callbacks check cancellation during Boolean construction, but individual validity and AABB-building phases are not immediately interruptible. The existing configuration fingerprint and lifetime check remain responsible for rejecting obsolete results before display.

No result grants material adhesion, load-bearing capacity, collision clearance, a machine motion schedule or executable output. The immutable hybrid-preview export guard is unaffected.

The backend builds in the existing `libslic3r_cgal` target to retain CGAL's floating-point requirements. The local dependency is CGAL 6.2.1; its series documentation covers [Boolean operations](https://doc.cgal.org/6.2/PMP_Boolean_operations/index.html) and [AABB distance queries](https://doc.cgal.org/6.2/AABB_tree/index.html).
