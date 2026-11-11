#pragma once
#include "FormorixGeometry.hpp"
#include "FormorixProcess.hpp"
#include "PrintConfig.hpp"
#include "ExtrusionEntity.hpp"
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
    double width = 0., height = 0., mm3_per_mm = 0.;
    ExtrusionRole role = erNone;
    bool support       = false;
};
struct Result
{
    std::vector<Path> paths;
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
             const std::function<bool()>& canceled = {});
} // namespace Slic3r::FormorixCorePreview
