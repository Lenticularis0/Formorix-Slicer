// Three-dimensional surface contours. No height-field or machine-reachability assumption.
#pragma once
#include "FormorixGeometry.hpp"
#include "Point.hpp"
#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace Slic3r::FormorixSurfacePaths {
struct Input
{
    FormorixGeometry::Mesh surface;
    std::vector<Vec3d> vertex_normals; // one finite nonzero vector per source vertex
};
struct Options
{
    // Offsets are signed millimetres along the normalized plane_normal.
    Vec3d plane_normal = Vec3d::UnitY();
    std::vector<double> plane_offsets;
    std::size_t max_faces = 200000, max_planes = 2000, max_knots = 500000;
    std::size_t max_carrier_candidates = 2000000;
    double max_segment_length          = .5;
    // Bound interpolation of p(t)-d*n(t) for every |d| up to this distance.
    double normal_offset_distance  = 1.;
    double interpolation_tolerance = .001;
};
struct Knot
{
    Vec3d position = Vec3d::Zero(), normal = Vec3d::UnitZ();
};
struct Carrier
{
    int face = -1; // ORIGINAL input triangle, not a nearest-surface approximation
    std::array<double, 3> from_barycentric{}, to_barycentric{};
};
struct Run
{
    std::size_t plane_index = 0;
    bool closed             = false;
    std::vector<Knot> knots; // closed runs have unique knots, no duplicated endpoint
    // N entries for closed runs (last->first included), otherwise N-1.
    std::vector<Carrier> carriers;
};
enum class Status { Complete, InvalidInput, InvalidMesh, ResourceLimit, Canceled, Failure };
struct Result
{
    Status status = Status::InvalidInput;
    std::string reason;
    std::vector<std::string> warnings;
    std::vector<Run> runs;
    std::size_t isolated_contacts = 0, carrier_candidates = 0;
    double position_rounding_bound    = std::numeric_limits<double>::infinity();
    double normal_rounding_bound      = std::numeric_limits<double>::infinity(); // dimensionless
    double offset_interpolation_bound = std::numeric_limits<double>::infinity();
    bool interpolation_bounded        = false;
    bool ok() const { return status == Status::Complete; }
};
// Exact mesh/plane intersections with explicit triangle carriers. Each returned
// run is a distinct extrusion candidate. Never connects across a hole, component,
// plane or region. Plane spacing is NOT a geodesic material-coverage certificate.
Result trace(const Input&, const Options&, const std::function<bool()>& canceled = {});
} // namespace Slic3r::FormorixSurfacePaths
