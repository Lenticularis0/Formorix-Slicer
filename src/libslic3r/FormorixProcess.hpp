// Nominal process, dependency and machine-envelope foundations. No motion approval.
#pragma once
#include "Point.hpp"
#include "FormorixGeometry.hpp"
#include <nlohmann/json_fwd.hpp>
#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Slic3r {
class ConfigOptionResolver;
}
namespace Slic3r::FormorixProcess {
// Explicit requested-width limits in mm. Absence permits only the nominal width;
// these are declared process inputs, not a physical calibration certificate.
struct WidthLimits
{
    double minimum_mm = 0., maximum_mm = 0.;
};
bool valid(const WidthLimits&);
void to_json(nlohmann::json&, const WidthLimits&);
void from_json(const nlohmann::json&, WidthLimits&);
struct Profile
{
    std::string material_id, nozzle_profile;
    double nozzle_diameter = .4, line_width = .45, layer_height = .2;
    double temperature = 210., volumetric_limit = 10., speed = 30., flow_ratio = 1.;
    // Ratios, not percentages. Stored separately from geometry: no implicit scaling of CAD.
    std::array<double, 3> shrink_compensation{{1., 1., 1.}};
    double width_correction = 1., height_correction = 1.;
    double contact_to_tcp = 0.; // mm along nozzle axis
    std::optional<WidthLimits> local_width_limits;
    // Keep filament flow_ratio separate from Orca's process and role factors.
    double print_flow_ratio = 1., surface_flow_ratio = 1.;
};
void to_json(nlohmann::json&, const Profile&);
void from_json(const nlohmann::json&, Profile&);
bool valid(const Profile&);
double effective_surface_flow(const Profile&);
// Resolve explicit nozzle/material vector indices; never silently use nozzle zero.
Profile from_orca(const ConfigOptionResolver&,
                  std::size_t nozzle_index,
                  std::size_t material_index,
                  const std::string& material_id,
                  const std::string& nozzle_profile,
                  double line_width,
                  double layer_height,
                  double speed);
double section_area(const Profile&);
double effective_speed(const Profile&);
struct Bead
{
    Vec3d center, tcp, contact, width_axis, height_axis;
    double half_width = 0., half_height = 0.;
    Vec3d point(double angle) const;
    Vec3d support(const Vec3d& normal) const;
};
// Elliptic cross-section normal to the travel tangent. Tilt is represented by
// projecting the nozzle axis into that plane. It is a nominal, uncalibrated model.
Bead place_bead(const Vec3d& cad_contact, const Vec3d& normal, const Vec3d& tangent, const Vec3d& nozzle_axis, const Profile&);
struct ContactCurve
{
    std::function<Vec3d(double)> contact, normal, tangent, nozzle_axis;
    // Bound for the compensated bead CENTER curve, not the original CAD curve.
    std::function<double(double, double)> center_second_derivative_bound;
};
struct DepositionKnot
{
    double parameter = 0.;
    Bead bead;
    Vec3d normal, nozzle_axis;
};
struct DepositionPath
{
    int head = 0, region = 0;
    Profile profile;
    std::vector<DepositionKnot> knots;
    double center_interpolation_bound = 0.;
    bool center_bounded               = false;
};
// A single supplied CAD contact curve, not a surface coverage or multihead planner.
DepositionPath compensate_curve(const ContactCurve&,
                                double first,
                                double last,
                                int head,
                                int region,
                                const Profile&,
                                double tolerance         = .001,
                                std::size_t max_segments = 100000);
// Plane-only scallop rule. Curvature/tilt require independent envelope measurement.
double planar_spacing(const Profile&, double maximum_scallop);
struct BoundaryContact
{
    double center_spacing  = 0.;
    bool adhesion_verified = false;
};
BoundaryContact nominal_boundary_contact(const Profile&, const Profile&);

enum class OperationKind { Core, DenseTransition, Solid, Breakaway, Soluble, FullWall, Surface, Reinforcement };
enum class Compatibility { Unknown, Compatible, Incompatible };
struct Operation
{
    int id = 0, region = 0, head = 0;
    std::string material;
    OperationKind kind = OperationKind::Core;
};
struct Dependency
{
    int prerequisite = 0, dependent = 0;
};
struct Dependencies
{
    std::vector<Operation> operations;
    std::vector<Dependency> edges;
    bool valid() const;
    bool prerequisites_complete(int operation, const std::set<int>& completed) const;
};
struct UnderlayEvidence
{
    // Conservative maximum gap over the WHOLE bead footprint, including sampling error.
    double gap_upper_bound = 0., allowed_gap = 0.;
    bool coverage_bounded = false, finished = false;
    Compatibility compatibility = Compatibility::Unknown;
    bool nominally_supported() const; // not a mechanical strength certificate
};

struct Capsule
{
    Vec3d a, b;
    double radius = 0.;
};
struct NozzleGeometry
{
    bool placeholder  = true;
    double tip_radius = .2, cone_length = 8., cone_radius = 4., shaft_length = 15., shaft_radius = 4.;
    Vec3d housing_offset  = Vec3d(0., 0., 35.);
    double housing_radius = 25.; // circumscribed sphere until CAD dimensions are measured
};
struct HeadMachine
{
    Vec3d rotation_center = Vec3d::Zero();
    Vec3d pivot_to_tcp    = Vec3d(0., 0., -35.);
    Vec3d minimum = Vec3d(0., 0., 0.), maximum = Vec3d(1600., 350., 100.);
    double max_tilt_degrees = 75.;
    NozzleGeometry nozzle;
};
struct Machine
{
    std::array<HeadMachine, 3> heads;
    double bed_z_min = 0., bed_z_max = 350.;
};
struct Envelope
{
    Vec3d tcp, axis;
    std::vector<Capsule> shapes;
    bool placeholder = true;
    // An envelope is never a movement/collision clearance.
};
Envelope forward_envelope(const Machine&, int head, const Vec3d& stage_xyz, double yaw_degrees, double tilt_degrees, double bed_z);
// Broad phase only, including placeholders: true means narrow-phase checks needed.
bool bounds_may_overlap(const Envelope&, const Envelope&);
bool bounds_may_overlap(const Envelope&, const FormorixGeometry::Mesh& part);
} // namespace Slic3r::FormorixProcess
