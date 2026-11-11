#include "FormorixProcess.hpp"
#include "FormorixDeposition.hpp"
#include "FormorixGeometry.hpp"
#include "Config.hpp"
#include "Point.hpp"
#include <nlohmann/json.hpp>
#include <Eigen/Geometry>
#include <Eigen/Core>
#include <utility>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace Slic3r::FormorixProcess {
namespace {
constexpr double pi = 3.14159265358979323846;
bool between(double x, double a, double b) { return std::isfinite(x) && x >= a && x <= b; }
void require(const Profile& p)
{
    if (!valid(p))
        throw std::invalid_argument("Invalid per-head deposition profile");
}
} // namespace
void to_json(nlohmann::json& j, const Profile& p)
{
    j = nlohmann::json{{"material_id", p.material_id},
                       {"nozzle_profile", p.nozzle_profile},
                       {"nozzle_diameter", p.nozzle_diameter},
                       {"line_width", p.line_width},
                       {"layer_height", p.layer_height},
                       {"temperature", p.temperature},
                       {"volumetric_limit", p.volumetric_limit},
                       {"speed", p.speed},
                       {"flow_ratio", p.flow_ratio},
                       {"shrink_compensation", p.shrink_compensation},
                       {"width_correction", p.width_correction},
                       {"height_correction", p.height_correction},
                       {"contact_to_tcp", p.contact_to_tcp}};
}
void from_json(const nlohmann::json& j, Profile& p)
{
    j.at("material_id").get_to(p.material_id);
    j.at("nozzle_profile").get_to(p.nozzle_profile);
    j.at("nozzle_diameter").get_to(p.nozzle_diameter);
    j.at("line_width").get_to(p.line_width);
    j.at("layer_height").get_to(p.layer_height);
    j.at("temperature").get_to(p.temperature);
    j.at("volumetric_limit").get_to(p.volumetric_limit);
    j.at("speed").get_to(p.speed);
    j.at("flow_ratio").get_to(p.flow_ratio);
    j.at("shrink_compensation").get_to(p.shrink_compensation);
    j.at("width_correction").get_to(p.width_correction);
    j.at("height_correction").get_to(p.height_correction);
    j.at("contact_to_tcp").get_to(p.contact_to_tcp);
    require(p);
}
bool valid(const Profile& p)
{
    if (p.material_id.empty() || p.material_id.size() > 4096 || p.nozzle_profile.empty() || p.nozzle_profile.size() > 4096 ||
        !between(p.nozzle_diameter, .05, 5.) || !between(p.line_width, .01, 10.) || !between(p.layer_height, .001, p.nozzle_diameter) ||
        p.layer_height > p.line_width || !between(p.temperature, 0., 500.) || !between(p.volumetric_limit, .001, 1000.) ||
        !between(p.speed, .001, 2000.) || !between(p.flow_ratio, .1, 3.) || !between(p.width_correction, .5, 2.) ||
        !between(p.height_correction, .5, 2.) || !between(p.contact_to_tcp, -20., 20.))
        return false;
    for (double s : p.shrink_compensation)
        if (!between(s, .5, 2.))
            return false;
    return true;
}
Profile from_orca(const ConfigOptionResolver& config,
                  std::size_t nozzle,
                  std::size_t material,
                  const std::string& id,
                  const std::string& nozzle_id,
                  double width,
                  double height,
                  double speed)
{
    const auto* diameters    = config.option<ConfigOptionFloats>("nozzle_diameter");
    const auto* temperatures = config.option<ConfigOptionInts>("nozzle_temperature");
    const auto* flow         = config.option<ConfigOptionFloats>("filament_flow_ratio");
    const auto* volume       = config.option<ConfigOptionFloats>("filament_max_volumetric_speed");
    const auto* xy           = config.option<ConfigOptionPercents>("filament_shrink");
    const auto* z            = config.option<ConfigOptionPercents>("filament_shrinkage_compensation_z");
    if (!diameters || nozzle >= diameters->values.size() || !temperatures || material >= temperatures->values.size() || !flow ||
        material >= flow->values.size() || !volume || material >= volume->values.size() || !xy || material >= xy->values.size() || !z ||
        material >= z->values.size())
        throw std::invalid_argument("Missing indexed Orca nozzle/material profile values");
    Profile p;
    p.material_id      = id;
    p.nozzle_profile   = nozzle_id;
    p.nozzle_diameter  = diameters->values[nozzle];
    p.temperature      = temperatures->values[material];
    p.flow_ratio       = flow->values[material];
    p.volumetric_limit = volume->values[material];
    p.line_width       = width;
    p.layer_height     = height;
    p.speed            = speed;
    // Orca stores expected printed size in percent; compensation is the reciprocal.
    p.shrink_compensation = {100. / xy->values[material], 100. / xy->values[material], 100. / z->values[material]};
    require(p);
    return p;
}
double section_area(const Profile& p)
{
    require(p);
    return pi * p.line_width * p.width_correction * p.layer_height * p.height_correction / 4.;
}
double effective_speed(const Profile& p) { return std::min(p.speed, p.volumetric_limit / (section_area(p) * p.flow_ratio)); }
Vec3d Bead::point(double t) const { return center + half_width * std::cos(t) * width_axis + half_height * std::sin(t) * height_axis; }
Vec3d Bead::support(const Vec3d& normal) const
{
    const Vec3d q  = half_width * half_width * normal.dot(width_axis) * width_axis +
                     half_height * half_height * normal.dot(height_axis) * height_axis;
    const double d = normal.dot(q);
    if (!normal.allFinite() || !std::isfinite(d) || d <= 1e-20)
        throw std::invalid_argument("Normal is parallel to bead tangent");
    return center + q / std::sqrt(d);
}
Bead place_bead(const Vec3d& cad, const Vec3d& normal, const Vec3d& tangent, const Vec3d& axis, const Profile& p)
{
    require(p);
    if (!cad.allFinite() || !normal.allFinite() || !tangent.allFinite() || !axis.allFinite() || normal.norm() < 1e-12 ||
        tangent.norm() < 1e-12 || axis.norm() < 1e-12)
        throw std::invalid_argument("Invalid bead frame");
    const Vec3d n = normal.normalized(), t = tangent.normalized(), a = axis.normalized();
    if (std::abs(n.dot(t)) > 1e-8 || a.dot(n) <= 0.)
        throw std::invalid_argument("Invalid contact normal/tangent");
    Vec3d h = a - t * a.dot(t);
    if (h.norm() < 1e-9)
        throw std::invalid_argument("Nozzle parallel to travel");
    Bead bead;
    bead.height_axis = h.normalized();
    bead.width_axis  = t.cross(bead.height_axis).normalized();
    bead.half_width  = p.line_width * p.width_correction / 2.;
    bead.half_height = p.layer_height * p.height_correction / 2.;
    bead.center      = Vec3d::Zero();
    bead.center      = cad - bead.support(n);
    bead.contact     = cad;
    bead.tcp         = cad + a * p.contact_to_tcp;
    return bead;
}
double planar_spacing(const Profile& p, double error)
{
    require(p);
    const double b = p.layer_height * p.height_correction / 2.;
    if (!between(error, 1e-9, b))
        throw std::invalid_argument("Invalid scallop target");
    const double q = error / b;
    return p.line_width * p.width_correction * std::sqrt(2. * q - q * q);
}
CoverageCell planar_coverage(const Profile& p, double pitch)
{
    require(p);
    const double a = p.line_width * p.width_correction / 2., b = p.layer_height * p.height_correction / 2.;
    if (!std::isfinite(pitch) || pitch <= 0. || pitch > 2. * a)
        throw std::invalid_argument("Coverage has invalid pitch or lateral gaps");
    // Integrate the nearest bead over [-pitch/2,pitch/2]. This handles any
    // number of overlapping neighbours, unlike subtracting pairwise lenses.
    const double u = std::clamp(pitch / (2. * a), 0., 1.);
    CoverageCell out;
    out.pitch                 = pitch;
    out.supplied_area         = section_area(p) * p.flow_ratio;
    out.union_area            = 2. * a * b * (std::asin(u) + u * std::sqrt(1. - u * u));
    out.excess_area           = out.supplied_area - out.union_area;
    out.volume_balance_factor = out.union_area / out.supplied_area;
    return out;
}
RibbonSection ribbon_section(const Profile& p)
{
    require(p);
    const double width  = p.line_width * p.width_correction;
    const double height = p.layer_height * p.height_correction;
    if (width < height)
        throw std::invalid_argument("Nominal compacted ribbon requires width at least equal to height");
    RibbonSection out;
    out.requested_width = p.line_width;
    out.height          = height;
    // Same non-bridge stadium formula as Orca Flow, evaluated in double rather
    // than converting the profile dimensions through Flow's float members.
    out.area_before_flow = height * (width - height) + pi * height * height / 4.;
    out.supplied_area    = out.area_before_flow * p.flow_ratio;
    out.compacted_width  = out.supplied_area / height;
    return out;
}
double ribbon_speed(const Profile& p)
{
    const auto section = ribbon_section(p);
    return std::min(p.speed, p.volumetric_limit / section.supplied_area);
}
RibbonBalance ribbon_balance(double supplied, double target, double rounding_residual)
{
    if (!std::isfinite(supplied) || supplied < 0. || !std::isfinite(target) || target <= 0. || !std::isfinite(rounding_residual))
        throw std::invalid_argument("Invalid nominal ribbon volume ledger");
    RibbonBalance out;
    out.excess_volume       = std::max(0., supplied - target);
    out.numerical_tolerance = std::abs(rounding_residual) + 1e-8 * std::max(1., target);
    if (!std::isfinite(out.numerical_tolerance))
        throw std::invalid_argument("Unrepresentable ribbon volume tolerance");
    out.inadmissible = out.excess_volume > out.numerical_tolerance;
    // Compare the excess to ten percent, avoiding overflow in 1.1*target.
    out.gross_oversupply = out.excess_volume > .1 * target + out.numerical_tolerance;
    return out;
}
DepositionPath compensate_curve(const ContactCurve& curve,
                                double first,
                                double last,
                                int head,
                                int region,
                                const Profile& profile,
                                double tolerance,
                                std::size_t max_segments)
{
    require(profile);
    if (head < 0 || head > 2 || region <= 0 || !curve.contact || !curve.normal || !curve.tangent || !curve.nozzle_axis)
        throw std::invalid_argument("Invalid contact curve");
    auto bead = [&](double t) { return place_bead(curve.contact(t), curve.normal(t), curve.tangent(t), curve.nozzle_axis(t), profile); };
    auto line = FormorixGeometry::discretize({[&](double t) { return bead(t).center; }, curve.center_second_derivative_bound}, first, last,
                                             tolerance, max_segments);
    DepositionPath out;
    out.head                       = head;
    out.region                     = region;
    out.profile                    = profile;
    out.center_interpolation_bound = line.interpolation_bound;
    out.center_bounded             = line.bounded;
    for (const auto& p : line.samples)
        out.knots.push_back(
            {p.parameter, bead(p.parameter), curve.normal(p.parameter).normalized(), curve.nozzle_axis(p.parameter).normalized()});
    return out;
}
BoundaryContact nominal_boundary_contact(const Profile& a, const Profile& b)
{
    require(a);
    require(b);
    return {(a.line_width * a.width_correction + b.line_width * b.width_correction) / 2., false};
}
bool Dependencies::valid() const
{
    if (operations.size() > 10000 || edges.size() > 100000)
        return false;
    std::map<int, int> indegree;
    for (const auto& op : operations)
        if (op.id <= 0 || op.region < 0 || op.head < 0 || op.head > 2 || op.material.empty() || int(op.kind) < 0 || int(op.kind) > 7 ||
            !indegree.emplace(op.id, 0).second)
            return false;
    std::set<std::pair<int, int>> unique;
    for (const auto& e : edges) {
        if (!indegree.count(e.prerequisite) || !indegree.count(e.dependent) || !unique.emplace(e.prerequisite, e.dependent).second)
            return false;
        ++indegree[e.dependent];
    }
    std::vector<int> available;
    for (const auto& e : indegree)
        if (e.second == 0)
            available.push_back(e.first);
    for (std::size_t i = 0; i < available.size(); ++i)
        for (const auto& e : edges)
            if (e.prerequisite == available[i] && --indegree[e.dependent] == 0)
                available.push_back(e.dependent);
    return available.size() == operations.size();
}
bool Dependencies::prerequisites_complete(int id, const std::set<int>& completed) const
{
    if (!valid() || std::none_of(operations.begin(), operations.end(), [id](const Operation& op) { return op.id == id; }))
        return false;
    // Check the transitive closure too: inconsistent completion records cannot bypass an underlay.
    std::set<int> visited;
    std::vector<int> pending{id};
    while (!pending.empty()) {
        int node = pending.back();
        pending.pop_back();
        if (!visited.insert(node).second)
            continue;
        for (const auto& e : edges)
            if (e.dependent == node) {
                if (!completed.count(e.prerequisite))
                    return false;
                pending.push_back(e.prerequisite);
            }
    }
    return true;
}
bool UnderlayEvidence::nominally_supported() const
{
    return coverage_bounded && finished && compatibility == Compatibility::Compatible && std::isfinite(gap_upper_bound) &&
           std::isfinite(allowed_gap) && gap_upper_bound >= 0. && allowed_gap >= 0. && gap_upper_bound <= allowed_gap;
}
Envelope forward_envelope(const Machine& machine, int head, const Vec3d& xyz, double yaw, double tilt, double bed)
{
    if (head < 0 || head > 2 || !xyz.allFinite() || !between(yaw, -360., 360.) || !std::isfinite(tilt) ||
        !between(machine.bed_z_min, 0., 10000.) || !between(machine.bed_z_max, machine.bed_z_min, 10000.) ||
        !between(bed, machine.bed_z_min, machine.bed_z_max))
        throw std::invalid_argument("Invalid machine command");
    const auto& h = machine.heads[head];
    const auto& g = h.nozzle;
    if (!h.minimum.allFinite() || !h.maximum.allFinite() || (h.minimum.array() > h.maximum.array()).any() ||
        (xyz.array() < h.minimum.array()).any() || (xyz.array() > h.maximum.array()).any() || !between(h.max_tilt_degrees, 0., 90.) ||
        std::abs(tilt) > h.max_tilt_degrees || !h.rotation_center.allFinite() || !h.pivot_to_tcp.allFinite() ||
        !g.housing_offset.allFinite() || !between(g.tip_radius, .001, 100.) || !between(g.cone_radius, g.tip_radius, 200.) ||
        !between(g.cone_length, .001, 500.) || !between(g.shaft_radius, .001, 200.) || !between(g.shaft_length, .001, 500.) ||
        !between(g.housing_radius, .001, 500.))
        throw std::invalid_argument("Invalid machine envelope/axis limit");
    const Eigen::Matrix3d rotation =
        (Eigen::AngleAxisd(yaw * pi / 180., Vec3d::UnitZ()) * Eigen::AngleAxisd(tilt * pi / 180., Vec3d::UnitY())).toRotationMatrix();
    Envelope e;
    e.placeholder    = g.placeholder;
    e.axis           = rotation * Vec3d::UnitZ();
    e.tcp            = xyz + h.rotation_center + rotation * h.pivot_to_tcp - Vec3d(0., 0., bed);
    const Vec3d neck = e.tcp + e.axis * g.cone_length, end = neck + e.axis * g.shaft_length;
    e.shapes.push_back({e.tcp, neck, g.cone_radius});
    e.shapes.push_back({neck, end, g.shaft_radius});
    const Vec3d housing = e.tcp + rotation * g.housing_offset;
    e.shapes.push_back({housing, housing, g.housing_radius});
    return e;
}
bool bounds_may_overlap(const Envelope& a, const Envelope& b)
{
    auto box = [](const Envelope& e) {
        if (e.shapes.empty())
            throw std::invalid_argument("Empty envelope");
        Eigen::AlignedBox3d box;
        for (const auto& s : e.shapes) {
            if (!s.a.allFinite() || !s.b.allFinite() || !std::isfinite(s.radius) || s.radius <= 0.)
                throw std::invalid_argument("Invalid capsule");
            const Vec3d r = Vec3d::Constant(s.radius);
            box.extend(s.a - r);
            box.extend(s.a + r);
            box.extend(s.b - r);
            box.extend(s.b + r);
        }
        return box;
    };
    return box(a).intersects(box(b));
}
bool bounds_may_overlap(const Envelope& envelope, const FormorixGeometry::Mesh& part)
{
    if (part.vertices.empty() || envelope.shapes.empty())
        throw std::invalid_argument("Empty collision candidate");
    Eigen::AlignedBox3d part_box;
    for (const auto& p : part.vertices) {
        if (!p.allFinite())
            throw std::invalid_argument("Invalid part coordinate");
        part_box.extend(p);
    }
    for (const auto& s : envelope.shapes) {
        if (!s.a.allFinite() || !s.b.allFinite() || !std::isfinite(s.radius) || s.radius <= 0.)
            throw std::invalid_argument("Invalid capsule");
        Eigen::AlignedBox3d box;
        const Vec3d r = Vec3d::Constant(s.radius);
        box.extend(s.a - r);
        box.extend(s.a + r);
        box.extend(s.b - r);
        box.extend(s.b + r);
        if (box.intersects(part_box))
            return true;
    }
    return false;
}
} // namespace Slic3r::FormorixProcess
