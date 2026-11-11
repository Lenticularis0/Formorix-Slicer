# Surface contours and source carriers

`FormorixSurfacePaths` constructs three-dimensional contours directly on a selected triangulated surface. It has no global height-field assumption and does not flatten a wrapped surface into a single-valued projection. It is independent of conventional Orca slicing, machine kinematics, execution and scheduling.

## Exact contours

The input is a double-precision mesh with one finite, nonzero normal-field vector per source vertex. The caller supplies a plane-family direction and explicit signed offsets in millimetres along its normalized direction. Duplicate offsets are rejected because they would repeat the same material contour. The original mesh and normal field are immutable.

The CGAL backend uses `Polygon_mesh_slicer` with exact predicates and constructions on the supplied binary coordinates. It accepts open manifold surfaces, holes and wrapped surfaces. Self-intersections, nonmanifold topology, degenerate faces and inconsistent normal fields at coincident vertices are rejected. Exact-equal geometric vertices are joined without moving their positions; inconsistent duplicates are not averaged to force a result.

The slicer produces independent polylines. Each nonzero segment must have a source triangle that contains both endpoints exactly. A face AABB tree supplies candidates, but nearest-point proximity is never sufficient to accept a carrier. If no single-triangle carrier exists, the result is rejected. Convexity of a triangle then ensures that the entire segment remains on that triangle. Original source-face IDs and both endpoint barycentric coordinates accompany every emitted segment.

Crossing a triangle boundary does not end an extrusion run. A hole, disconnected contour or change of slicing plane produces a separate run, without an implicit connecting extrusion. The adapter can insert an explicitly unverified travel movement between such runs. Closed contours contain unique knots and a final last-to-first carrier. They require cyclic material miters; two unrelated flat end caps would introduce an artificial seam.

When a plane coincides with faces, CGAL reports contour boundaries rather than an area-filling pattern. Such cases are reported separately and never establish complete surface coverage. Isolated contacts have no extrusion length. An empty result has no positive interpolation certificate.

## Adaptive interpolation bound

Within a source triangle, barycentric interpolation gives an affine, unnormalized normal field

```
u(t) = u0 + t * du,       0 <= t <= 1
n(t) = u(t) / |u(t)|
```

The backend evaluates the minimum squared norm of `u` over the whole interval using exact arithmetic. A vanishing minimum is rejected, including when the requested offset distance is zero: the nozzle orientation would still be undefined.

For `m = min |u| > 0`, differentiating the normalized field gives the conservative bound

```
|n''(t)| <= 6 * |du|^2 / m^2
```

The contact curve `p(t)` is a straight segment on its carrier. Consequently, linear interpolation of the nominal offset curve `p(t) - d*n(t)` has error at most `|d| * 6*|du|^2/m^2 / 8`. The module applies this to every offset magnitude up to the caller's declared `normal_offset_distance`. It subdivides source segments until both the requested spatial segment length and this complete interval bound are satisfied.

Exact points and normal-field vectors are converted to doubles with separate outward-rounded displacement bounds. Normalization uses component intervals and a conservative Euclidean bound from the sum of component errors. The reported offset bound includes endpoint position and normal-conversion effects, not just a midpoint sample. Deep lazy-expression chains are avoided by materializing exact constants during refinement.

The bound refers to the supplied piecewise-affine normal field, not to an unknown analytic CAD normal. A caller performing additional floating-point offset arithmetic must also account for that arithmetic; the reported bound concerns the mathematical interpolation of the returned data. It does not prove coverage between neighboring contours. Physical material shape, adhesion, collision clearance and machine reachability are outside this module.

## Adapter and validation contract

The hybrid adapter retains its existing connected raster paths and finite boundary termination wherever the height-field planner supports the surface. It invokes `FormorixSurfacePaths` as an extension for `NonDisk`, `NotHeightField` or `Disconnected` results, with zero boundary margin. A nonzero margin on a wrapped or otherwise spatial-only surface is explicitly unsupported. Rectangular boundary construction remains a separate earlier candidate path. This selection preserves the existing continuous turns of supported curved raster patches instead of replacing them with separate plane contours.

The hybrid adapter consumes the returned double coordinates and normals directly. Reprojecting them with a nearest-surface query would lose their original-carrier guarantee. Original triangle identities remain available for diagnostics and validation across merged surface regions.

Automatic plane-family selection may choose an exact coordinate axis when its numerical direction differs only within the declared floating-point allowance. Offsets are generated symmetrically about the span midpoint. These choices stabilize the sampling geometry without moving source vertices or modifying the material audit. Distinct contour runs remain separated by travel with zero supply. A closed preview repeats its first point for display and timing; the material run retains unique stations and a cyclic closing segment.

The plane-family spacing is an explicit sampling choice, not generally the geodesic distance between material lines. In particular, spacing along an extrusion axis is useful for complete cylindrical or wing-profile cross-sections, but does not by itself establish a valid material partition on an arbitrary curved surface. The actual three-dimensional material union, target difference, overlaps and complete boundary-distance audit remain mandatory.

A suitable planar hole can pass that complete spatial audit, while a wrapped contour can remain only diagnostic. Neither outcome proves general curved-surface filling, finite-cap correctness at arbitrary curved boundaries, or full material coverage between all contours.

No mechanical tilt range is imposed or certified here. A complete wrapped profile can contain inaccessible orientations for a particular machine. Its nose, trailing edge or reverse-facing surface must not silently disappear to make a reachability result look successful.

## Limits and failure behavior

Input-face, plane, knot and carrier-candidate budgets bound data sizes. Adaptive refinement has a finite depth limit. A failure clears all runs and positive interpolation evidence; it does not return a partially successful contour set as complete. Cancellation is checked during input conversion, candidate traversal and refinement.

CGAL's self-intersection check, AABB construction and each plane-slicing call are not immediately interruptible internally. One slicing call can temporarily collect all contours of that plane before the output-knot budget is checked. The mesh budget bounds this work, but it is not a hard wall-clock or memory guarantee.

The standalone tests exercise triangle-boundary continuity, double curvature, polygon holes, wrapped cylinders, a complete closed NACA0012 profile, normal-offset refinement, singular normal fields, coplanar cuts, invalid input and resource exhaustion. Their measured interpolation errors supplement the conservative interval bound; they do not replace it or establish physical printing accuracy.
