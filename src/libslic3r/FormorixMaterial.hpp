// Nominal three-dimensional material evidence. AGPL-3.0-or-later.
#pragma once
#include "FormorixGeometry.hpp"
#include "Point.hpp"
#include <cmath>
#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::FormorixMaterial {
struct Station
{
    Vec3d center = Vec3d::Zero();
    Vec3d axis   = Vec3d::UnitZ();
    double width = 0., height = 0.; // nominal compacted dimensions, not requested nozzle width
    // Optional shared lower/upper section centers, explicitly constructed by the
    // planner. They are authoritative input coordinates; the audit never snaps them.
    std::optional<std::array<Vec3d, 2>> height_endpoints;
};
// Split travel and profile changes BEFORE creating a run. No implicit extrusion.
struct SweepRun
{
    int head = 0, region = 0, layer = 0;
    std::string material;
    std::vector<Station> stations;
    std::vector<double> supplied_volumes; // one physical nominal supply per segment, mm^3
    bool outer_layer = false;
};
struct Target
{
    int region = 0;
    std::string material;
    FormorixGeometry::Mesh mesh;
    FormorixGeometry::Mesh outer_surface; // optional marked CAD/mesh surface, may be open
};
struct Options
{
    std::size_t max_stations           = 500000;
    std::size_t max_sweep_faces        = 600000;
    std::size_t max_boolean_faces      = 200000;
    std::size_t max_boolean_operations = 20000;
    std::size_t max_distance_leaves    = 200000;
    std::size_t max_distance_queries   = 50000;
    double distance_slack              = .001; // requested interval width, mm
    double miter_limit                 = 4.;
};
enum class Status { Complete, InvalidInput, InvalidSweep, BooleanFailure, ResourceLimit, Canceled };
enum class Coverage { Empty, Partial, Full }; // Full means target-minus-material is exactly empty
struct Distance
{
    double maximum = std::numeric_limits<double>::infinity();
    double mean = std::numeric_limits<double>::infinity(), rms = std::numeric_limits<double>::infinity();
    double lower_bound  = 0.;
    double upper_bound  = std::numeric_limits<double>::infinity();
    std::size_t samples = 0;
    bool bounded        = false; // full two-sided boundary coverage, not a CAD/physical accuracy claim
};
struct Region
{
    int region = 0;
    std::string material;
    Coverage coverage    = Coverage::Empty;
    double target_volume = 0., covered_volume = 0., missing_volume = 0.;
    double deposited_volume = 0., outside_volume = 0.;
    double supplied_volume = 0., cell_volume = 0., duplicate_volume = 0., core_overlap_volume = 0.;
    double supply_geometry_residual = 0., input_rounding_volume_bound = 0.;
    FormorixGeometry::Mesh missing, outside;
    Distance boundary_distance;
    Distance nominal_outer_distance; // roof sheets only; not automatically the exterior of the union
};
struct MaterialOverlap
{
    std::string first, second;
    double volume = 0.;
    FormorixGeometry::Mesh mesh;
};
struct Result
{
    Status status = Status::InvalidInput;
    std::string error;
    // The untrimmed nominal sweep remains diagnostic geometry if its audit fails.
    // It is a collection of closed cells, NOT an already-unioned manifold solid.
    FormorixGeometry::Mesh diagnostic_cells;
    FormorixGeometry::Mesh deposited, missing, outside, core_overlap;
    FormorixGeometry::Mesh nominal_outer_sheet;
    std::vector<Region> regions;
    std::vector<MaterialOverlap> material_overlaps;
    double supplied_volume = 0., cell_volume = 0., union_volume = 0.;
    double duplicate_volume         = 0.; // integral of max(multiplicity-1,0), not pair-intersection sum
    double supply_geometry_residual = 0.; // supplied minus sum of geometric cell volumes
    double missing_volume = 0., outside_volume = 0., core_overlap_volume = 0.;
    double rounding_bound              = 0.; // construction rounding + exact-to-output displacement, mm
    double construction_rounding_bound = 0., exact_to_output_bound = 0.;
    double input_rounding_volume_bound = 0.; // reserved; raw cells are never snapped, currently zero
    double output_volume_residual      = 0.; // volume of rounded union minus exact union
    Distance boundary_distance;
    Distance nominal_outer_distance;
    std::size_t cells = 0, boolean_operations = 0, peak_boolean_faces = 0;
    std::size_t distance_queries = 0;
    bool ok() const { return status == Status::Complete; }
};
// Exact relative to the supplied piecewise-linear cells and target mesh. Never
// clips or repairs the material to obtain a favourable result. No export/scheduler.
Result audit(const std::vector<Target>&,
             const FormorixGeometry::Mesh& core,
             const std::vector<SweepRun>&,
             const Options&                        = {},
             const std::function<bool()>& canceled = {});
} // namespace Slic3r::FormorixMaterial
