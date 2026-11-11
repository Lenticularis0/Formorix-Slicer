# Hydrofoil input inspection — Auftrag 05

This report records measurements of the user's actual FreeCAD-exported negative
mould, made on 2026-11-11 with Python's standard `zipfile`, XML, `math`, `struct`
and hashing libraries. It is an input inspection, not a successful slicing or
machine-reachability test. No CAD reference or tessellation tolerance accompanies
the file; its CAD error remains unknown.

## Original and reproducible fixture

Original local file:

`C:\Formorix Slicer\Test Objekte\Fluke_Hydrofoil_Negativform_Oberseite-Negativform Oberseite - Fluke DIY Hydrofoil001.3mf`

The byte-identical repository fixture is
[hydrofoil-negative-original.3mf](../../../../tests/data/formorix/hydrofoil-negative-original.3mf).
The original was never modified. SHA-256 was checked before and after copying,
and separately on the copied fixture; all three values matched:

```text
bytes: 301886
SHA256: 297a6dbd7dbf89e2bc01e427ad3bcba9ff292db4968a90cb82b6a893314a3d9a
```

The archive contains one `3D/3dmodel.model`, ordinary relationship/content-type
files and a thumbnail. It declares millimetres, `Application=FreeCAD`, one model
object with ID 1 and an identity build transform. There are no Orca or Formorix
project settings or surface-paint markings. Every triangle has only `v1/v2/v3`
attributes. There is no B-rep or analytic surface embedded in this archive.

## Whole input mesh

| Measurement | Result |
|---|---:|
| Vertices / triangles | 11,709 / 23,414 |
| Unique vertex coordinate tuples | 11,709 |
| Undirected edges | 35,121 |
| Edges with exactly two incident triangles | 35,121 |
| Inconsistently directed shared edges | 0 |
| Zero-area triangles | 0 |
| Euler characteristic | 2 |
| Signed volume | 2,511,830.6046853564 mm³ |
| Triangle area sum | 269,976.30798724096 mm² |
| X range | −35.0111 to 135.0 mm |
| Y range | −327.3 to 352.7 mm |
| Z range | 0.133894 to 29.003 mm |

The dimensions are 170.0111 × 680 × 28.869106 mm. These incidence and orientation
checks establish a consistently oriented closed topological surface; they do not
prove the absence of geometric self-intersections. No CGAL self-intersection or
solid-validity check was executed as part of this Python inspection.

The broad flat back is at Z = 29.003 mm. The actual negative working surface faces
downward in the supplied orientation. The archive thumbnail mainly shows the back,
so selecting the visible flat top would test the wrong surface.

## Full negative-surface selection

The following selection retains the entire working surface, including the leading
edge/nose, trailing edge and both span ends. No chordwise inset or nose/tail crop is
used:

1. Calculate each original triangle's oriented unit normal in double precision.
2. Build adjacency through shared undirected indexed edges.
3. Flood from original zero-based face 1557, accepting a neighbour when the dot
   product of adjacent unit normals is at least `cos(45°)`.

Face 1557 has centroid `(49.9004, 15.385033333333332, 13.273866666666668)` mm.
Face 0, centroid `(-13.253566666666666, 62.29593333333333, 4.0048)`, gives the same
component. These indices describe the original XML ordering; use the centroid when
an importer changes indexing.

This is the existing GUI's **Connected surface** concept: enable stopping at geometric
edges, set the edge angle to 45°, and click within the central working surface after
orienting it toward the viewer. `TriangleSelector::seed_fill_select_triangles` uses
adjacent-face normal comparisons. The Python selection is stable from 40° through
60°; at 30° and 35° it omits small patches. The minimum angle across its external
boundary is 84.558638° (maximum 176.639861°). GUI selection and archive round trip
must still be exercised separately; this inspection is not a GUI-test claim.

| Selected patch | Result |
|---|---:|
| Triangles / vertices / edges | 21,196 / 10,696 / 31,891 |
| Boundary edges | 194 |
| Degree of every boundary vertex | 2 |
| Euler characteristic | 1 |
| Surface area | 79,703.83670326127 mm² |
| X range | −15.0017 to 135.0 mm |
| Y range | −327.3 to 352.7 mm |
| Z range | 0.133894 to 14.0023 mm |

The SHA-256 of the sorted original selected triangle indices, each packed as a
little-endian unsigned 32-bit integer, is:

```text
531d283f163cdf40658d96d68c3c98e3a5ed86b34ad6f5bd095f922243f7ea6f
```

The selection is a connected topological disk. That does **not** make it a height
field or establish a distortion-free planar parameterization.

## Orientation, nose and trailing edge

For a reproducible placement on the 1600 × 350 mm bed, the rigid transform

```text
X' =  Y + 327.3
Y' =  X + 35.0111
Z' = 29.003 − Z
```

has determinant +1, puts the flat back on Z' = 0, and fits the complete mould into
a 680 × 170.0111 mm footprint. It is equivalent to an X rotation of 180° followed
by a Z rotation of 90° and translation. It does not change scale, shape or winding.

Plane sections at the following original Y positions each produced one continuous
open working-surface curve. Intersections were connected by source edge identities,
without geometric welding. These include both chordwise endpoints:

| Original Y (mm) | Section nodes | Nose endpoint (X, Z), mm | Trailing endpoint (X, Z), mm |
|---:|---:|---:|---:|
| −320 | 43 | (86.708096, 0.794658) | (125.692818, 0.479088) |
| −160 | 52 | (5.827618, 2.497414) | (125.867875, 1.599803) |
| 12.7 | 93 | (−15, 2.99734) | (135, 2) |
| 160 | 52 | (−0.165665, 2.67232) | (127.459344, 1.699758) |
| 345 | 55 | (85.853402, 0.863939) | (125.676661, 0.488461) |

After this orientation, 200 selected facets have a downward-facing normal, with
69.632931 mm² total area. Thus the imported selected mesh has returning portions
that cannot be represented by a single Z-height field. These are measured mesh
properties; without the CAD reference, intentional undercuts cannot be distinguished
from tessellation artefacts. They must not be silently discarded either way.

Facet inclinations after orientation exceed 75° on 2,767 triangles (203.258923 mm²)
and exceed 105° on 22 triangles (10.339458 mm²). The largest facet inclination is
approximately 121.44°. The area-weighted vertex normals currently used by the hybrid
planner have a maximum inclination of 107.024136°: vertex 5699, original position
`(115.958, −327.3, 0.200663)`. Of the selected vertex normals, 1,312 exceed 75°,
147 exceed 90° and three exceed 105°.

Consequently the complete selected mesh cannot receive a positive reachability
statement from the present 75° tilt plus at most 15° deviation settings. The rigid
nominal section currently also rejects nonperpendicular deposition. An algorithmic
geometry-only test may explicitly separate these restrictions, but must not present
that test as a machine motion approval.

## Resolution and accuracy evidence

The selected patch's edge-length distribution is:

| Minimum | Median | 90th percentile | 99th percentile | Maximum |
|---:|---:|---:|---:|---:|
| 0.006182362 | 0.755596620 | 4.935101517 | 14.972036193 | 47.358001395 |

All values are millimetres; quantiles select the nearest indexed value in the sorted
edge lengths. This strongly varying mesh density cannot by itself certify a maximum
CAD approximation error. Large nearly planar faces may be suitable, but the supplied
data has no analytic curvature/error bound against which to verify that assumption.

Converting all XML double-valued vertex coordinates to IEEE float32 and measuring
Euclidean displacement gives:

| Vertex displacement | mm |
|---|---:|
| Maximum | 0.000015564907548331925 |
| Mean | 0.000004171471225791026 |
| RMS | 0.00000570561140153894 |

This is solely an input-format conversion measurement, not the unknown CAD-import
error, a shell/path error or real printing accuracy. No 0.01 mm CAD claim is justified
for this file without additional reference data.

## Required test strategy

Use the unchanged full model and the full selected component for the principal
regression. Check selection count/hash, orientation, continuous mesh-plane sections
through the whole chord, and retained returning facets. Report unsupported geometry,
normal limits and resource limits explicitly; a central cropped patch is not a pass
for the complete hydrofoil.

For diagnosis, independent full-chord sections at the five span positions above and
thin span bands around them are appropriate. Each retains its local nose and trailing
edge. Separate leading-edge, trailing-edge and span-end diagnostic subsets may help
locate failures, but results must remain linked to the uncut principal test.

For a known-CAD accuracy baseline, add a complete analytic NACA fixture with the
leading edge, trailing edge and both upper/lower branches. Existing tests are weaker:
`test_formorix_foundations.cpp::foil_curve` traces the complete upper NACA0012 curve
using x = 100t², but is not a wrapped surface; the hybrid fixture uses chord fractions
0.125–0.875 before its additional paint inset, and the preview fixture also excludes
the endpoints. Neither cropped surface fixture verifies whole-foil continuity.

For subsequent native-GUI acceptance, open the copied fixture, orient it without
scaling, use the 45° connected selection, save/reload the project, then inspect nose,
trailing edge and both span ends. Compare any displayed nominal deposition and
missing/outside bodies with the same full spatial material-audit output. GUI execution,
new surface-contour tests and a release build are separate work, not results claimed
by this report.
