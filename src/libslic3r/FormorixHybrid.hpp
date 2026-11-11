#pragma once
#include "FormorixGeometry.hpp"
#include "FormorixMaterial.hpp"
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
    FormorixMaterial::Options material_options;
    std::size_t max_motions = 500000; // caller may lower the fixed preview resource ceiling
};
struct FloatMesh
{
    indexed_triangle_set mesh;
    double displacement_bound = 0.;
};
FloatMesh to_float(const FormorixGeometry::Mesh&);
// Add nonnegative finite upper bounds with outward rounding. Invalid/unknown
// operands and overflow return infinity; adding zero preserves an exact term.
double add_error_bounds(double, double);
struct Accuracy
{
    // CAD import, shell construction, float/Orca conversion, path interpolation, nominal deposition.
    std::array<double, 5> bounds{{INFINITY, INFINITY, INFINITY, INFINITY, INFINITY}};
    bool whole_surface_covered       = false;
    bool material_volume_plausible   = false;
    bool material_partition_verified = false; // completed all-layer spatial identity, separate from a coarse volume ratio
    // Conservatively rounded sum; infinity also denotes an invalid individual bound.
    double total_bound() const;
    bool verified(double tolerance = .01) const;
    // Replace audit-dependent evidence, invalidating the previous material budget
    // and flags first. The caller supplies the current region/material identity;
    // CAD, shell, conversion, interpolation and coarse plausibility stay separate.
    void update_material_evidence(const FormorixMaterial::Result&, int region, const std::string& material, bool inadmissible);
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
    std::size_t missing_core = 0, unsuitable_infill = 0, path_gaps = 0, unknown_geometry = 0;
    std::size_t native_layer_gaps = 0; // even a fully filled actual Orca layer cannot reach this sample
    std::size_t raw_missing = 0; // raw native geometry only, disregarding uncalibrated nonunit supply
    std::size_t no_nearby_contact = 0;
    double maximum_gap            = 0.;
    double maximum_finite_gap     = 0.;    // observed contact within the finite search range, not a global nearest-distance bound
    bool coverage_bounded         = false; // sample success alone never proves a complete footprint
};
enum class UnderlayKind { Contact, MissingCore, ExcludedInfill, PathGap, UnknownGeometry, NativeLayerGap };
struct UnderlayContact
{
    Vec3d position = Vec3d::Zero(), inward = Vec3d::Zero();
    UnderlayKind kind = UnderlayKind::UnknownGeometry;
    double gap = INFINITY, raw_gap = INFINITY;
    // Index into the actually supplied UnderlaySegment vector; no synthetic contact.
    std::size_t path_index = std::numeric_limits<std::size_t>::max();
    bool raw_geometry_unknown = false;
};
struct RegionResult
{
    int id = 0, head = 0;
    FormorixGeometry::Mesh surface;
    FormorixProcess::Profile process;
    FormorixSurfaces::Settings settings;
    double shell_depth = 0., shell_volume = 0., deposited_volume = 0., volume_ratio = 0., pitch = 0.;
    double nominal_core_overlap  = 0.; // sampled projection past the local core interface, not a global collision certificate
    std::string deposition_model = "CoalescedOrcaStadiumRibbon/v1";
    double compacted_width = 0., supplied_area = 0.;
    double minimum_local_width = INFINITY, maximum_local_width = 0.;
    double maximum_boundary_remainder = 0.;
    bool adaptive_boundary = false;
    std::string boundary_diagnostic;
    std::size_t contour_runs = 0, closed_contours = 0;
    bool inadmissible_material        = false;
    double excess_material_volume_mm3 = 0.;
    // Projected diagnostics, not certified 3D volumes on curved surfaces.
    double projected_union_volume = 0., projected_uncovered_volume = 0., projected_outside_volume = 0., projected_overlap_volume = 0.;
    Accuracy accuracy;
    Coverage coverage;
    Underlay underlay;
    std::vector<Vec3d> interface_samples;
    std::vector<Vec3d> interface_normals;
    std::vector<UnderlayContact> underlay_contacts;
};
struct Result
{
    FormorixGeometry::Mesh original, core;
    FormorixGeometry::Partition partition;
    FormorixMaterial::Result material; // actual nominal 3D cells; diagnostic failures never confer accuracy
    std::vector<FormorixMaterial::SweepRun> material_runs;
    FormorixPreview::Result preview; // material centers in layers; actual TCPs in the 5D-only timeline
    std::vector<RegionResult> regions;
    std::vector<Vec3d> missing_underlay, unverified;
    std::vector<Vec3d> missing_core, unsuitable_infill, path_gaps, unknown_underlay;
    std::vector<Vec3d> native_layer_gaps;
    FormorixProcess::Dependencies manufacturing_dependencies; // geometric requests, never a completed schedule
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
    bool bridge  = false; // hanging bead placement is not certified by the supported rounded-rectangle audit
    bool flow_geometry_unverified = false; // changed supply has no calibrated native cross-section
    bool dense_transition = false; // actual native modifier region, not an inferred/fictitious path
};
// Geometric contact against actual Orca extrusion sweeps. No strength/adhesion/schedule claim.
void audit_underlay(Result&, const std::vector<UnderlaySegment>&, const std::function<bool()>& canceled = {});
} // namespace FormorixHybrid
} // namespace Slic3r
