#pragma once
#include "FormorixGeometry.hpp"
#include "FormorixProcess.hpp"
#include "PrintConfig.hpp"
#include "ExtrusionEntity.hpp"
#include "ExPolygon.hpp"
#include "Point.hpp"
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Slic3r::FormorixCorePreview {
struct Source
{
    DynamicPrintConfig config;
    std::size_t nozzle_index = 0, material_index = 0;
    FormorixProcess::Profile process;
};
struct Path
{
    std::vector<Vec3d> points; // source model coordinates; top of the conventional bead
    int head = 0;
    FormorixProcess::Profile process;
    double width = 0., height = 0., mm3_per_mm = 0.; // native geometric flow, before export-only ratios
    double role_flow_ratio = 1., first_layer_flow_ratio = 1.;
    // Static Orca export factors applied once; no small-area or dynamic G-code compensation.
    double nominal_supplied_mm3_per_mm = 0.;
    // Static flow changes do not establish a new deposited cross-section.
    bool flow_geometry_unverified = false;
    ExtrusionRole role = erNone;
    bool support       = false;
    bool dense_transition = false; // actual native region density exceeds the unchanged core baseline
    double native_infill_density = 0.; // actual LayerRegion percentage; zero for the separate support pass
};
struct LayerSlice
{
    int layer_id = 0, head = 0;
    ExPolygons area; // scaled source XY; geometric slice, not deposited-material coverage
    double slice_z = 0., bottom_z = 0., top_z = 0.; // source-mesh coordinates, without raft lift
    double print_bottom_z = 0., print_top_z = 0.;   // actual native deposition coordinates, including raft lift
    ExPolygons region_area; // exact union of native raw LayerRegion slices, before lslices safety inflation
};
struct DenseRequest
{
    int region = 0;
    ExPolygons area; // scaled source XY, must be contained in the identified native slice
    double bottom_z = 0., top_z = 0., slice_z = 0., density = 0.; // geometric source Z and percent
};
struct Result
{
    std::vector<Path> paths;
    std::vector<LayerSlice> layers;
    std::vector<int> applied_dense_regions;
    std::size_t dense_requests_applied = 0, dense_modifier_volumes = 0;
    std::size_t dense_modifier_triangles = 0;
    double modifier_float_conversion_bound = 0., modifier_volume_error = 0.;
    double native_slice_expansion_bound = 0.; // native lslices safety offset, separately from Float/grid conversion
    // Extent allowed for construction offsets; NOT a whole-mask Hausdorff bound
    // when thin mask components disappear. Never enters the CAD error budget.
    double modifier_boundary_retreat_bound = 0.;
    double dense_requested_area = 0., dense_applied_area = 0.; // disjoint summed areas over requested native layers, mm^2
    double float_conversion_bound        = 0.;
    double coordinate_quantization_bound = 0.;
    double converted_volume_residual     = 0.;
    Vec3d local_origin                   = Vec3d::Zero();
    std::vector<std::string> warnings;
    std::string error;
    bool ok() const { return error.empty() && !paths.empty(); }
};
// Actual Orca Print processing, without calling any GCode exporter. A separate
// support-head pass is geometric evidence only, not a synchronized print schedule.
Result slice(const FormorixGeometry::Mesh&,
             const std::array<Source, 3>&,
             int core_head,
             int support_head,
             const std::function<bool()>& canceled = {},
             const std::vector<DenseRequest>& requests = {});
} // namespace Slic3r::FormorixCorePreview
