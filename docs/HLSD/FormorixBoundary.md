# Finite planar ribbon boundaries

`FormorixBoundary` lays out finite nominal 5D ribbons in a planar rectangle. It is independent of the conventional Orca slicing engine, GUI and machine execution. Its output is a candidate for the spatial `FormorixMaterial` audit, not a material-coverage or accuracy certificate.

## Domain and supported geometry

The caller supplies a triangulated surface in a double-precision orthonormal plane frame. X is the requested raster direction. The caller must establish that the original surface is planar before using this layout; projecting a curved surface does not make it a supported rectangle.

The module examines the unrounded projected mesh, exact-equal vertex coordinates, triangle orientation, shared edges, connectivity and the boundary contour. A single rectangular disk is supported. Holes, separate components, nonmanifold or folded projections, sloping boundaries and reentrant contours are rejected with a reason. This recognition deliberately avoids integer-grid polygon conversion: a small hole must not disappear during classification. The existing exact three-dimensional material audit remains the authority for the resulting volume.

An optional, explicit rectangle-classification tolerance allows the caller to account for projection arithmetic. Its boundary deviation is returned separately. The default is zero. This tolerance does not repair or alter the original target mesh, and cannot make missing or outside material pass the spatial audit.

## Width, placement and finite ends

The input profile belongs to one actual layer and retains its nozzle, material, layer height, speed, temperature, volumetric limit and flow factors. Without explicit local-width limits, its requested line width cannot change. The largest fitting integer number of fixed ribbons is centered in the rectangle; remaining strips are returned as actual unfilled rectangles. A region narrower than one ribbon is rejected.

With explicit inclusive limits, the module chooses a feasible integer row count close to the nominal count. It solves for one local requested width for this rectangle and layer. There is no continuously varying width along a run. For corrected layer height `h`, width correction `k`, and the unchanged effective flow product `f`, the nominal coalesced width is

```
p = (k*w - h*(1 - pi/4)) * f
w = (p/f + h*(1 - pi/4)) / k
```

The process helper recomputes the actual section from that local width and checks its explicit limits. Infeasible intervals are rejected. No missing volume authorizes modifying Flow. The local profile is a copy; the source profile remains unchanged.

Rows share canonical transverse boundaries. The serpentine uses flat end caps on the longitudinal target boundaries and mitered turns half a compacted width inside the side boundaries. This includes the connector volume: for the ideal uniform rectangle, the entire centerline length is `row_count * rectangle_width`, including turns. Moving every horizontal row all the way to the side before adding connectors would over-supply material and is not the construction used here.

Canonical lateral miter endpoints are returned with each station. They prevent independently evaluated `center +/- width/2` operations from creating microscopic internal gaps at decimal coordinates. The material frontend validates these endpoints against the original station's nominal miter before any simplification. It uses the supplied geometry without snapping or repairing it. A separate construction-displacement bound and the spatial supply/geometry residual expose the remaining arithmetic effects.

## Quantities and integration

Supply is the actual path length times the local section's supplied area. The latter includes the unchanged filament, print and applicable outer-wall flow factors. The speed limit is computed from that actual area and the configured volumetric throughput. The hybrid adapter must retain the local profile on every motion and convert material-center supply to TCP distance when the two path lengths differ.

Every layer is planned independently because a different layer height changes the section and allowed row count. All resulting layers must then pass the same spatial union, missing-volume, outside-volume, duplicate-volume, core-overlap and distance checks. A successful outer layer cannot certify a different inner layer.

`Status::Ready` means a finite path was constructed. `full_candidate` describes the rectangular construction and is never a replacement for `FormorixMaterial::Coverage::Full`. An explicit remainder remains incomplete. Invalid inputs, unsupported boundaries, cancellation and resource exhaustion have distinct statuses and clear their paths. Station, row and input-triangle budgets bound the module's data sizes.

This module does not implement arbitrary polygon boundary filling, curved-surface filling, universal variable-width extrusion, physical coalescence calibration, support generation, scheduling or executable output.
