#pragma once
#include "FormorixGeometry.hpp"
#include "FormorixPreview.hpp"
#include "FormorixProcess.hpp"
#include "FormorixSurfaces.hpp"
#include "Point.hpp"
#include "TriangleMesh.hpp"
#include <array>
#include <cstddef>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {
class ModelObject;
namespace FormorixHybrid {
struct RegionInput
{
    int id = 0, head = 0;
    double max_tilt = 75.;
    FormorixGeometry::Mesh surface;
    FormorixSurfaces::Settings settings;
    FormorixProcess::Profile process;
    std::optional<FormorixGeometry::Reference> cad_reference;
};
struct Input
{
    FormorixGeometry::Mesh solid;
    std::vector<RegionInput> regions;
};
struct FloatMesh
{
    indexed_triangle_set mesh;
    double displacement_bound = 0.;
};
FloatMesh to_float(const FormorixGeometry::Mesh&);
struct Accuracy
{
    // CAD import, shell construction, float/Orca conversion, path interpolation, nominal deposition.
    std::array<double, 5> bounds{{INFINITY, INFINITY, INFINITY, INFINITY, INFINITY}};
    bool whole_surface_covered     = false;
    bool material_volume_plausible = false;
    bool verified(double tolerance = .01) const;
};
struct Coverage
{
    double uncovered_fraction = 1.;
    double outside_fraction   = 0.;
    double sampled_maximum = 0., sampled_mean = 0., sampled_rms = 0.;
    std::size_t samples = 0, missing_samples = 0;
    bool bounded = false;
};
struct Underlay
{
    std::size_t samples = 0, missing = 0;
    double maximum_gap    = 0.;
    bool coverage_bounded = false; // sample success alone never proves a complete footprint
};
struct RegionResult
{
    int id = 0, head = 0;
    FormorixGeometry::Mesh surface;
    FormorixProcess::Profile process;
    FormorixSurfaces::Settings settings;
    double shell_depth = 0., shell_volume = 0., deposited_volume = 0., volume_ratio = 0., pitch = 0.;
    double nominal_core_overlap = 0.; // sampled projection past the local core interface, not a global collision certificate
    Accuracy accuracy;
    Coverage coverage;
    Underlay underlay;
    std::vector<Vec3d> interface_samples;
    std::vector<Vec3d> interface_normals;
};
struct Result
{
    FormorixGeometry::Mesh original, core;
    FormorixGeometry::Partition partition;
    FormorixPreview::Result preview; // material centers in layers; actual TCPs in the 5D-only timeline
    std::vector<RegionResult> regions;
    std::vector<Vec3d> missing_underlay, unverified;
    std::vector<std::string> warnings;
    std::string error;
    bool ok() const { return error.empty() && partition.ok() && preview.ok(); }
};
// All marked regions of one instance, including hidden regions; original model is never modified.
Input extract(const ModelObject&,
              int instance,
              const FormorixSurfaces::Project&,
              const std::array<FormorixProcess::Profile, 3>& freshly_resolved);
Result generate(const Input&, const std::function<bool()>& canceled = {});
struct UnderlaySegment
{
    Vec3d from, to; // conventional nozzle positions, z is the TOP of the deposited layer
    double width = 0., height = 0.;
    int head = 0;
    std::string material;
    bool support = false;
    bool infill  = false; // sparse conventional infill, excluded when region.use_infill is false
};
// Geometric contact against actual Orca extrusion sweeps. No strength/adhesion/schedule claim.
void audit_underlay(Result&, const std::vector<UnderlaySegment>&, const std::function<bool()>& canceled = {});
} // namespace FormorixHybrid
} // namespace Slic3r
