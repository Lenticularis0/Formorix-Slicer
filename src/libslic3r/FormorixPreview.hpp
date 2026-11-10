#pragma once
#include "Point.hpp"
#include "TriangleMesh.hpp"
#include "FormorixSurfaces.hpp"
#include <array>
#include <cstddef>
#include <functional>
#include <vector>

namespace Slic3r {
class ModelObject;
namespace FormorixPreview {
// Preview coordinates are world-space millimetres; axis points from the TCP into the nozzle.
struct Pose
{
    Vec3d position = Vec3d::Zero();
    Vec3d axis     = Vec3d::UnitZ();
};
enum class MotionKind { Extrude, Travel, Wait, Unassigned };
struct Motion
{
    int head     = 0;
    double start = 0., end = 0.;
    Pose from, to;
    MotionKind kind = MotionKind::Extrude;
    bool extrusion  = true;
    int layer = 0, region = 0;
};
struct HeadState
{
    Pose pose;
    MotionKind kind     = MotionKind::Unassigned;
    bool extrusion      = false;
    std::size_t segment = 0;
};
struct Timeline
{
    std::array<std::vector<Motion>, 3> heads;
    std::array<Pose, 3> idle;
    double duration() const;
    bool valid() const;
    HeadState sample(int head, double seconds) const;
};
enum class Issue {
    Empty,
    InvalidMesh,
    Disconnected,
    NonDisk,
    NotHeightField,
    ThinWall,
    InvalidOffset,
    FullWallVariable,
    VerticalDisabled,
    TiltExceeded,
    TooLarge,
    Canceled,
    StalePaint,
    MultipleHeads,
    ProfileMissing,
    ReferenceEdge
};
struct Input
{
    indexed_triangle_set surface;
    indexed_triangle_set solid;
    indexed_triangle_set remainder;
};
struct Options
{
    int head = 0, region = 1;
    double line_width = .4, speed = 30., travel_speed = 80., max_tilt = 75.;
    std::size_t max_segments = 500000;
};
struct Layer
{
    double depth = 0.;
    indexed_triangle_set surface;
    std::vector<Pose> path;
};
struct Result
{
    std::vector<Layer> layers;
    indexed_triangle_set selected, core;
    Timeline timeline;
    std::vector<Issue> errors;
    double shell_depth = 0.;
    bool full_wall     = false;
    bool ok() const { return errors.empty() && !layers.empty(); }
};

// Extraction honours subdivided brush triangles and the selected instance transform.
Input extract(const ModelObject& object, int instance, int region);
Vec3d reference_direction(const ModelObject&, int instance, const FormorixSurfaces::Settings&);
// Bounded, cancellable single-head planner. No Print/GCode/config export integration.
Result generate(const Input&, const FormorixSurfaces::Settings&, const Options&, const std::function<bool()>& canceled = {});
Vec3d interpolate_axis(const Vec3d& a, const Vec3d& b, double fraction);
} // namespace FormorixPreview
} // namespace Slic3r
