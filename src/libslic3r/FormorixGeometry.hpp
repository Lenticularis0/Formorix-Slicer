// Formorix geometric foundations. AGPL-3.0-or-later.
#pragma once
#include "Point.hpp"
#include <array>
#include <cstddef>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace Slic3r::FormorixGeometry {
// Never round through indexed_triangle_set (float) in this module. Units: mm.
struct Mesh
{
    std::vector<Vec3d> vertices;
    std::vector<Vec3i32> faces;
};
struct ShellRequest
{
    int region = 0;
    Mesh surface;
    std::vector<Vec3d> outward_normals;
    std::vector<double> depths; // one per vertex, shared by all adjacent faces
};
struct VolumePart
{
    int region = 0;
    Mesh mesh;
    double volume = 0.;
};
struct Interface
{
    int first = 0, second = 0;
    Mesh mesh; // oriented outwards from first; 0 identifies the core
};
struct Partition
{
    std::vector<VolumePart> parts;
    std::vector<Interface> interfaces; // one shared triangulation, opposite orientations in the solids
    double input_volume = 0., volume_residual = 0.;
    double rounding_bound     = 0.; // includes shared 2^-30 mm output grid; largest vertex displacement bound
    std::size_t mitered_edges = 0, mitered_vertices = 0;
    double miter_constraint_residual = 0.; // normal-distance residual in mm
    std::string error;
    bool ok() const { return error.empty() && !parts.empty(); }
};
// Validates closed oriented two-manifolds and self-intersections with CGAL.
bool valid_solid(const Mesh&);
double volume(const Mesh&);
Mesh shell_cutter(const ShellRequest&);
// Exact predicates/constructions until final double materialisation. Overlapping
// region interiors and cutters leaving the source solid are rejected, not repaired.
Partition partition(const Mesh& solid, const std::vector<ShellRequest>&);
enum class SharedBoundaryPolicy { RejectOverlap, MiterAdjacentEdges };
// Trim sweeps to the ORIGINAL solid. Explicit mitering gives adjacent regions a
// shared inner seam by intersecting their local offset planes. It never changes
// outer vertices or mixes materials; non-adjacent overlap remains an error.
// Miter length is limited to 4 times the largest incident requested thickness.
// Caller must separately audit wall thickness and deposition within each part.
Partition partition_clipped(const Mesh& solid, const std::vector<ShellRequest>&, SharedBoundaryPolicy = SharedBoundaryPolicy::RejectOverlap);

struct Deviation
{
    double maximum = 0., mean = 0., rms = 0.; // measured, area-weighted quadrature
    double upper_bound  = std::numeric_limits<double>::infinity();
    std::size_t samples = 0;
    bool bounded        = false;
    bool within(double tolerance) const;
};
struct Reference
{
    // Must be a true signed Euclidean distance (1-Lipschitz), not an implicit residual.
    std::function<double(const Vec3d&)> signed_distance;
    // A proven bound on the Hessian norm over the WHOLE triangle, or infinity.
    std::function<double(const Vec3d&, const Vec3d&, const Vec3d&)> hessian_bound;
    std::string identity;
    // Optional tighter interpolation remainder using directional curvature (e.g. cylinder axis).
    std::function<double(const Vec3d&, const Vec3d&, const Vec3d&)> linearization_error;
};
Reference plane(const Vec3d& origin, const Vec3d& normal);
Reference sphere(const Vec3d& center, double radius);
Reference cylinder(const Vec3d& origin, const Vec3d& axis, double radius);
// Directed mesh -> analytic surface distance. Not a reverse-coverage/Hausdorff proof.
// Without a reference there is no CAD accuracy claim. Limits produce bounded=false.
Deviation measure(const Mesh&, const Reference&, double bound_slack = .001, std::size_t max_leaves = 200000);
struct AccuracyBudget
{
    // Imported CAD error, shell error, path interpolation, nominal deposition error.
    std::array<double, 4> upper_bounds{{INFINITY, INFINITY, INFINITY, INFINITY}};
    bool nominal_verified(double tolerance = .01) const;
    // Real printing remains unverified, regardless of this numerical result.
};
struct CurveSample
{
    double parameter = 0.;
    Vec3d point      = Vec3d::Zero();
};
struct Curve
{
    std::function<Vec3d(double)> point;
    // Supremum of |d² point / dt²| over [a,b], not just a midpoint estimate.
    std::function<double(double, double)> second_derivative_bound;
};
struct Polyline
{
    std::vector<CurveSample> samples;
    double interpolation_bound = 0.;
    bool bounded               = false;
};
Polyline discretize(const Curve&, double first, double last, double tolerance, std::size_t max_segments = 100000);

// Exact per-triangle charts are a conservative atlas for arbitrary manifold topology.
// They need no global height-field assumption; chart boundaries are not print seams.
struct Portal
{
    int first_face = 0, second_face = 0;
    std::array<int, 2> edge{};
};
struct Atlas
{
    std::vector<Portal> portals;
    std::size_t boundary_edges = 0;
};
Atlas make_atlas(const Mesh&);
Vec3d lift(const Mesh&, int face, const Vec2d& barycentric);
struct PathPiece
{
    int face = 0, head = 0;
    std::string material;
    std::vector<Vec3d> points;
};
std::vector<Vec3d> join_across_charts(const Mesh&, const Atlas&, const std::vector<PathPiece>&, double tolerance = 1e-9);
} // namespace Slic3r::FormorixGeometry
