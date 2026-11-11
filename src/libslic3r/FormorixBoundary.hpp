// Finite planar 5D ribbon layout; no conventional slicing or machine approval.
#pragma once
#include "FormorixDeposition.hpp"
#include "FormorixProcess.hpp"
#include "Point.hpp"
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Slic3r::FormorixBoundary {
// Double coordinates in the caller's orthonormal plane frame. X is the requested
// raster direction. The original mesh remains the authority for spatial audit.
struct Domain
{
    std::vector<Vec2d> vertices;
    std::vector<Vec3i32> triangles;
};
struct Rectangle
{
    Vec2d minimum = Vec2d::Zero(), maximum = Vec2d::Zero();
};
struct Options
{
    std::size_t max_triangles = 200000, max_rows = 100000, max_points = 500000;
    // Explicit numerical classification allowance, never a replacement for the
    // material audit. Zero requires an exactly axis-aligned rectangular boundary.
    double rectangle_tolerance = 0.;
};
enum class Status { Ready, Unsupported, InvalidInput, ResourceLimit, Canceled };
struct Result
{
    Status status = Status::InvalidInput;
    std::string reason;
    Rectangle target, covered;
    std::vector<Rectangle> remainder;
    std::size_t rows = 0;
    FormorixProcess::Profile local_profile;
    FormorixProcess::RibbonSection section;
    double speed          = 0.;
    double residual_width = 0., supplied_volume = 0.;
    double boundary_deviation_bound = 0., construction_bound = 0.;
    bool width_adjusted = false;
    // Candidate describes the planar construction only. It is NOT a spatial
    // coverage or accuracy certificate; every layer still needs FormorixMaterial.
    bool full_candidate = false;
    std::vector<Vec2d> path;
    // Explicit canonical miter rails, ordered center-minus-side/center-plus-side.
    // Shared strip boundaries prevent independently reconstructed ULP-sized gaps.
    std::vector<std::array<Vec2d, 2>> lateral_endpoints;
    bool ok() const { return status == Status::Ready; }
};
// Uses one actual local width per rectangle/layer. Without explicit WidthLimits,
// the requested profile width is immutable and finite remainder strips remain.
// No amount of missing area authorizes changing configured Flow.
Result plan(const Domain&, const FormorixProcess::Profile&, const Options& = {}, const std::function<bool()>& canceled = {});
} // namespace Slic3r::FormorixBoundary
