#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixGeometry.hpp"
#include "libslic3r/FormorixProcess.hpp"
#include "libslic3r/FormorixDeposition.hpp"
#include "libslic3r/FormorixPreview.hpp"
#include "libslic3r/FormorixSurfaces.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Point.hpp"
#include <nlohmann/json.hpp>
#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;
namespace FG = Slic3r::FormorixGeometry;
namespace FP = Slic3r::FormorixProcess;
namespace PV = Slic3r::FormorixPreview;
namespace FS = Slic3r::FormorixSurfaces;
namespace {
constexpr double pi = 3.14159265358979323846;
FG::Mesh plane_patch(double x0, double x1, double y0, double y1, double z)
{ return {{{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}}, {{0, 1, 2}, {0, 2, 3}}}; }
FG::Mesh box(double x0 = 0., double x1 = 10., double y0 = 0., double y1 = 10., double top = 5.)
{
    return {
        {{x0, y0, 0.}, {x1, y0, 0.}, {x1, y1, 0.}, {x0, y1, 0.}, {x0, y0, top}, {x1, y0, top}, {x1, y1, top}, {x0, y1, top}},
        {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4}, {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}}};
}
FG::ShellRequest request(FG::Mesh patch, double depth, int id = 1)
{
    FG::ShellRequest r;
    r.region  = id;
    r.surface = std::move(patch);
    r.outward_normals.assign(r.surface.vertices.size(), Vec3d::UnitZ());
    r.depths.assign(r.surface.vertices.size(), depth);
    return r;
}
FP::Profile profile(int head)
{
    FP::Profile p;
    p.material_id      = std::array<std::string, 3>{{"PETG-id", "PVA-id", "PETG-CF-id"}}[head];
    p.nozzle_profile   = "nozzle-" + std::to_string(head);
    p.nozzle_diameter  = std::array<double, 3>{{.8, .4, .6}}[head];
    p.line_width       = p.nozzle_diameter * 1.1;
    p.layer_height     = p.nozzle_diameter * .5;
    p.temperature      = std::array<double, 3>{{240., 205., 250.}}[head];
    p.speed            = std::array<double, 3>{{120., 30., 60.}}[head];
    p.volumetric_limit = std::array<double, 3>{{20., 4., 12.}}[head];
    return p;
}
void metric(const std::string& name, const FG::Deviation& d)
{
    std::cout << "METRIC " << name << "," << std::setprecision(17) << d.maximum << "," << d.mean << "," << d.rms << "," << d.upper_bound
              << "," << d.samples << "," << d.bounded << "\n";
}
FG::Mesh curved_patch(double radius, double first, double last, int segments, bool concave = false)
{
    FG::Mesh m;
    for (int i = 0; i <= segments; ++i) {
        double t = first + (last - first) * i / segments;
        for (double y : {0., 1.})
            m.vertices.emplace_back(radius * std::sin(t), y, (concave ? -1. : 1.) * radius * std::cos(t));
    }
    for (int i = 0; i < segments; ++i) {
        m.faces.emplace_back(2 * i, 2 * i + 2, 2 * i + 1);
        m.faces.emplace_back(2 * i + 2, 2 * i + 3, 2 * i + 1);
    }
    return m;
}
std::pair<FG::Mesh, FG::ShellRequest> cylinder_solid(double radius, int count, double depth)
{
    FG::Mesh m;
    for (int i = 0; i < count; ++i) {
        double a = 2 * pi * i / count;
        for (double y : {0., 3.})
            m.vertices.emplace_back(radius * std::cos(a), y, radius * std::sin(a));
    }
    for (int i = 0; i < count; ++i) {
        int j = (i + 1) % count;
        m.faces.emplace_back(2 * i, 2 * i + 1, 2 * j);
        m.faces.emplace_back(2 * j, 2 * i + 1, 2 * j + 1);
    }
    auto r = request(m, depth);
    for (std::size_t i = 0; i < r.outward_normals.size(); ++i)
        r.outward_normals[i] = Vec3d(m.vertices[i].x(), 0., m.vertices[i].z()).normalized();
    m.vertices.emplace_back(0., 0., 0.);
    m.vertices.emplace_back(0., 3., 0.);
    for (int i = 0; i < count; ++i) {
        int j = (i + 1) % count;
        m.faces.emplace_back(2 * count, 2 * i, 2 * j);
        m.faces.emplace_back(2 * count + 1, 2 * j + 1, 2 * i + 1);
    }
    return {m, r};
}
double distance_to_segment(const Vec3d& p, const Vec3d& a, const Vec3d& b)
{
    Vec3d d = b - a;
    return (p - a - std::clamp((p - a).dot(d) / d.squaredNorm(), 0., 1.) * d).norm();
}
FG::Curve foil_curve()
{
    // Closed trailing-edge NACA 0012, chord 100 mm; x=100*t² removes the sqrt singularity.
    return {[](double t) -> Vec3d {
                double t2 = t * t;
                return {100. * t2, 0., 60. * (.2969 * t - .1260 * t2 - .3516 * t2 * t2 + .2843 * t2 * t2 * t2 - .1036 * t2 * t2 * t2 * t2)};
            },
            [](double, double b) {
                return std::hypot(200., 60. * (.252 + 12 * .3516 * b * b + 30 * .2843 * std::pow(b, 4) + 56 * .1036 * std::pow(b, 6)));
            }};
}
} // namespace

TEST_CASE("Planar shell and core share an exact interface without volume loss", "[FormorixFoundations]")
{
    auto result = FG::partition(box(), {request(plane_patch(0., 10., 0., 10., 5.), 1.)});
    INFO(result.error);
    REQUIRE(result.ok());
    REQUIRE(result.parts.size() == 2);
    REQUIRE(result.interfaces.size() == 1);
    CHECK_THAT(result.parts[0].volume, WithinAbs(100., 1e-10));
    CHECK_THAT(result.parts[1].volume, WithinAbs(400., 1e-10));
    CHECK_THAT(result.volume_residual, WithinAbs(0., 1e-12));
    CHECK(result.rounding_bound < 1e-12);
    for (const auto& p : result.parts)
        CHECK(FG::valid_solid(p.mesh));
    auto d = FG::measure(result.interfaces[0].mesh, FG::plane({0., 0., 4.}, Vec3d::UnitZ()));
    metric("planar_interface", d);
    CHECK(d.within(.01));
    const auto full_wall = FG::partition(box(), {request(plane_patch(0., 10., 0., 10., 5.), 5.)});
    REQUIRE(full_wall.ok());
    REQUIRE(full_wall.parts.size() == 1);
    CHECK_THAT(full_wall.parts[0].volume, WithinAbs(500., 1e-10));
}
TEST_CASE("Adjacent shell regions retain distinct depths and conserve their common solid", "[FormorixFoundations]")
{
    auto result = FG::partition(box(),
                                {request(plane_patch(0., 5., 0., 10., 5.), 1., 1), request(plane_patch(5., 10., 0., 10., 5.), 2., 2)});
    INFO(result.error);
    REQUIRE(result.ok());
    REQUIRE(result.parts.size() == 3);
    CHECK_THAT(result.parts[0].volume, WithinAbs(50., 1e-9));
    CHECK_THAT(result.parts[1].volume, WithinAbs(100., 1e-9));
    CHECK_THAT(result.parts[2].volume, WithinAbs(350., 1e-9));
    CHECK(result.interfaces.size() == 3);
    std::cout << "VOLUME adjacent_depths," << result.input_volume << "," << result.volume_residual << "," << result.rounding_bound << "\n";
}
TEST_CASE("Variable wall and shell thickness produce a closed core", "[FormorixFoundations]")
{
    auto solid            = box();
    solid.vertices[5].z() = 7.5;
    solid.vertices[6].z() = 7.5;
    FG::Mesh top          = {{solid.vertices[4], solid.vertices[5], solid.vertices[6], solid.vertices[7]}, {{0, 1, 2}, {0, 2, 3}}};
    auto r                = request(top, .5);
    r.depths              = {.5, 1., 1., .5};
    for (auto& n : r.outward_normals)
        n = Vec3d(-.25, 0., 1.).normalized();
    // Restrict the patch away from source side walls; outward-directed boundary offsets would leave the solid.
    r.surface = plane_patch(2., 8., 2., 8., 0.);
    for (auto& p : r.surface.vertices)
        p.z() = 5. + .25 * p.x();
    auto result = FG::partition(solid, {r});
    INFO(result.error);
    REQUIRE(result.ok());
    CHECK_THAT(result.input_volume, WithinAbs(625., 1e-9));
    CHECK_THAT(result.volume_residual, WithinAbs(0., 1e-10));
    CHECK(FG::valid_solid(result.parts.back().mesh));
    std::cout << "VOLUME variable_wall," << result.input_volume << "," << result.volume_residual << "," << result.rounding_bound << "\n";
}
TEST_CASE("A circumferential shell supports vertical faces and overhangs without a height field", "[FormorixFoundations]")
{
    auto [solid, r] = cylinder_solid(3., 64, .25);
    auto result     = FG::partition(solid, {r});
    INFO(result.error);
    REQUIRE(result.ok());
    double expected = 32. * std::sin(2 * pi / 64.) * (9. - 2.75 * 2.75) * 3.;
    // Output vertices share a 2^-30 mm grid. This volume tolerance (mm^3)
    // includes the resulting sub-nanometre boundary displacement.
    CHECK_THAT(result.parts[0].volume, WithinAbs(expected, 1e-7));
    CHECK(result.rounding_bound < 2e-9);
    CHECK_THAT(result.volume_residual, WithinAbs(0., 1e-7));
    std::cout << "VOLUME circumferential," << std::setprecision(17) << result.input_volume << "," << result.volume_residual << ","
              << result.rounding_bound << "\n";
    CHECK(FG::valid_solid(result.parts.back().mesh));
    const auto atlas = FG::make_atlas(r.surface);
    CHECK(atlas.boundary_edges == 128);
    auto d = FG::measure(r.surface, FG::cylinder(Vec3d::Zero(), Vec3d::UnitY(), 3.), .0005);
    metric("circumferential_import", d);
    CHECK(d.within(.01));
    REQUIRE(result.interfaces.size() == 1);
    const auto inner = FG::measure(result.interfaces[0].mesh, FG::cylinder(Vec3d::Zero(), Vec3d::UnitY(), 2.75), .0005);
    metric("circumferential_inner_interface", inner);
    CHECK(inner.within(.01));
}
TEST_CASE("Offset and Boolean failures never leave partially accepted material volumes", "[FormorixFoundations]")
{
    auto r      = request(plane_patch(0., 10., 0., 10., 5.), 6.);
    auto result = FG::partition(box(), {r});
    CHECK_FALSE(result.ok());
    CHECK(result.parts.empty());
    r.depths.assign(4, 1.);
    auto duplicate   = r;
    duplicate.region = 2;
    CHECK_FALSE(FG::partition(box(), {r, duplicate}).ok());
    auto open = box();
    open.faces.pop_back();
    CHECK_FALSE(FG::partition(open, {r}).ok());
    r.depths[0] = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS(FG::shell_cutter(r));
    auto [cylinder, thin] = cylinder_solid(.2, 16, .3);
    CHECK_FALSE(FG::partition(cylinder, {thin}).ok());
}
TEST_CASE("Adaptive CAD distance checks distinguish fine coarse and unverified geometry", "[FormorixFoundations]")
{
    const bool concave = GENERATE(false, true);
    auto ref           = FG::cylinder(Vec3d::Zero(), Vec3d::UnitY(), 3.);
    auto fine          = FG::measure(curved_patch(3., -.8, .8, 64, concave), ref, .0005);
    auto coarse        = FG::measure(curved_patch(3., -.8, .8, 2, concave), ref, .0005);
    metric(concave ? "concave_fine" : "convex_fine", fine);
    metric(concave ? "concave_coarse" : "convex_coarse", coarse);
    CHECK(fine.within(.01));
    CHECK(coarse.maximum > .01);
    CHECK_FALSE(coarse.within(.01));
    auto incomplete = FG::measure(curved_patch(3., -.8, .8, 2), ref, .00001, 4);
    CHECK_FALSE(incomplete.bounded);
    FG::AccuracyBudget unknown;
    CHECK_FALSE(unknown.nominal_verified());
    FG::AccuracyBudget measured;
    measured.upper_bounds = {fine.upper_bound, .001, .001, .002};
    CHECK(measured.nominal_verified());
    measured.upper_bounds[0] = coarse.upper_bound;
    CHECK_FALSE(measured.nominal_verified());
}
TEST_CASE("Small radii require tighter tessellation instead of an accuracy promise", "[FormorixFoundations]")
{
    auto d = FG::measure(curved_patch(.15, -1., 1., 64), FG::cylinder(Vec3d::Zero(), Vec3d::UnitY(), .15), .0005);
    metric("tight_radius_import", d);
    CHECK(d.within(.01));
    auto p         = profile(1);
    p.line_width   = .44;
    p.layer_height = .2;
    auto bead      = FP::place_bead({0., 0., .15}, Vec3d::UnitZ(), Vec3d::UnitY(), Vec3d::UnitZ(), p);
    // A precise mesh does not make a wide bead conform to this radius.
    const double error = std::abs(FG::cylinder(Vec3d::Zero(), Vec3d::UnitY(), .15).signed_distance(bead.point(0.)));
    CHECK(error > .01);
    std::cout << "LIMIT tight_radius_bead," << error << "\n";
}
TEST_CASE("A doubly curved spherical CAD patch has a bounded import error", "[FormorixFoundations]")
{
    FG::Mesh patch;
    constexpr int n         = 32;
    constexpr double radius = 5.;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            const double x = -1. + 2. * i / n, y = -1. + 2. * j / n;
            patch.vertices.emplace_back(x, y, std::sqrt(radius * radius - x * x - y * y));
        }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            int a = j * (n + 1) + i;
            patch.faces.emplace_back(a, a + 1, a + n + 2);
            patch.faces.emplace_back(a, a + n + 2, a + n + 1);
        }
    auto d = FG::measure(patch, FG::sphere(Vec3d::Zero(), radius), .0005);
    metric("sphere_two_curvatures", d);
    CHECK(d.within(.01));
    auto atlas = FG::make_atlas(patch);
    CHECK(atlas.boundary_edges == 4 * n);
}
TEST_CASE("Compensated elliptical beads follow the CAD contact rather than the TCP center", "[FormorixFoundations]")
{
    const int head     = GENERATE(0, 1, 2);
    auto p             = profile(head);
    p.contact_to_tcp   = .07;
    const Vec3d tilted = Eigen::AngleAxisd(15 * pi / 180., Vec3d::UnitY()) * Vec3d::UnitZ();
    const auto bead    = FP::place_bead({1., 2., 3.}, Vec3d::UnitZ(), Vec3d::UnitY(), tilted, p);
    CHECK((bead.support(Vec3d::UnitZ()) - Vec3d(1., 2., 3.)).norm() < 1e-12);
    CHECK((bead.center - bead.contact).norm() > .01);
    CHECK_THAT((bead.tcp - bead.contact).norm(), WithinAbs(.07, 1e-12));
    const double spacing = FP::planar_spacing(p, .005), b = p.layer_height / 2.;
    const double cusp = b * (1. - std::sqrt(1. - std::pow(spacing / p.line_width, 2)));
    CHECK_THAT(cusp, WithinAbs(.005, 1e-12));
    FG::Deviation d;
    d.bounded     = true;
    d.upper_bound = cusp;
    d.maximum     = cusp;
    double sum = 0., sum2 = 0.;
    for (int i = 0; i <= 1000; ++i) {
        double x = spacing * (double(i) / 1000. - .5);
        double e = b * (1. - std::sqrt(1. - std::pow(2 * x / p.line_width, 2)));
        sum += e;
        sum2 += e * e;
        ++d.samples;
    }
    d.mean = sum / d.samples;
    d.rms  = std::sqrt(sum2 / d.samples);
    metric("planar_bead_head_" + std::to_string(head), d);
    CHECK(d.within(.01));
    CHECK_FALSE(FP::nominal_boundary_contact(p, profile((head + 1) % 3)).adhesion_verified);
}
TEST_CASE("Adaptive compensated paths bound center interpolation on both curved sides", "[FormorixFoundations]")
{
    const bool concave = GENERATE(false, true);
    const double r = 10., sign = concave ? -1. : 1.;
    auto p         = profile(1);
    p.layer_height = .2;
    p.line_width   = .44;
    FP::ContactCurve c;
    c.contact                        = [=](double t) -> Vec3d { return {r * std::sin(t), 0., r * std::cos(t)}; };
    c.normal                         = [=](double t) -> Vec3d { return sign * Vec3d(std::sin(t), 0., std::cos(t)); };
    c.tangent                        = [](double t) -> Vec3d { return {std::cos(t), 0., -std::sin(t)}; };
    c.nozzle_axis                    = c.normal;
    c.center_second_derivative_bound = [=](double, double) { return r - sign * p.layer_height / 2.; };
    auto path                        = FP::compensate_curve(c, -.8, .8, 2, 7, p, .0005);
    REQUIRE(path.center_bounded);
    CHECK(path.center_interpolation_bound <= .0005);
    FG::Deviation d;
    double sum = 0., sum2 = 0.;
    for (std::size_t i = 1; i < path.knots.size(); ++i)
        for (int j = 0; j <= 20; ++j) {
            double f = double(j) / 20., t = path.knots[i - 1].parameter * (1 - f) + path.knots[i].parameter * f;
            Vec3d center  = path.knots[i - 1].bead.center * (1 - f) + path.knots[i].bead.center * f;
            Vec3d contact = center + c.normal(t) * p.layer_height / 2.;
            double e      = (contact - c.contact(t)).norm();
            d.maximum     = std::max(d.maximum, e);
            sum += e;
            sum2 += e * e;
            ++d.samples;
        }
    d.mean        = sum / d.samples;
    d.rms         = std::sqrt(sum2 / d.samples);
    d.upper_bound = path.center_interpolation_bound;
    d.bounded     = path.center_bounded;
    metric(concave ? "concave_compensated_contact" : "convex_compensated_contact", d);
    CHECK(d.within(.01));
    CHECK(path.profile.material_id == p.material_id);
    CHECK(path.head == 2);
    CHECK(path.region == 7);
    // This bound concerns contact along the supplied curve, not transverse coverage of an entire surface.
}
TEST_CASE("Nominal coverage exposes duplicate material volume instead of assuming a surface fit is enough", "[FormorixFoundations]")
{
    const int head     = GENERATE(0, 1, 2);
    auto p             = profile(head);
    const double pitch = FP::planar_spacing(p, .005);
    const auto cell    = FP::planar_coverage(p, pitch);
    CHECK(cell.excess_area > 0.);
    CHECK(cell.volume_balance_factor < 1.);
    CHECK_THAT(cell.supplied_area * cell.volume_balance_factor, WithinAbs(cell.union_area, 1e-12));
    // Independent midpoint integration of the cross-section union over one cell.
    double integral = 0.;
    for (int i = 0; i < 10000; ++i) {
        const double x = pitch * ((i + .5) / 10000. - .5);
        integral += p.layer_height * std::sqrt(1. - std::pow(2. * x / p.line_width, 2)) * pitch / 10000.;
    }
    const double error = std::abs(integral - cell.union_area);
    CHECK(error < 1e-9);
    const auto touching = FP::planar_coverage(p, p.line_width);
    CHECK_THAT(touching.excess_area, WithinAbs(0., 1e-12));
    CHECK_THROWS(FP::planar_coverage(p, p.line_width * 1.01));
    std::cout << "COVERAGE head_" << head << "," << std::setprecision(17) << pitch << "," << cell.supplied_area << "," << cell.union_area
              << "," << cell.volume_balance_factor << "," << error << "\n";
}
TEST_CASE("Curvature contributes a separate nominal material envelope error", "[FormorixFoundations]")
{
    const int head    = GENERATE(0, 1, 2);
    const double sign = GENERATE(-1., 1.);
    auto p            = profile(head);
    const double r = 3., a = p.line_width / 2., b = p.layer_height / 2.;
    const double spacing = FP::planar_spacing(p, .005);
    FG::Deviation d;
    double sum = 0., sum2 = 0.;
    for (int i = 0; i <= 2000; ++i) {
        double x = spacing * (double(i) / 2000. - .5), z = r - sign * b + sign * b * std::sqrt(1. - x * x / (a * a));
        double error = std::abs(std::hypot(x, z) - r);
        d.maximum    = std::max(d.maximum, error);
        sum += error;
        sum2 += error * error;
        ++d.samples;
    }
    // sqrt(z²+x²)-|z| <= x²/(2|z|); |z| >= R-b, plus the planar scallop.
    d.upper_bound = .005 + spacing * spacing / (8. * (r - b));
    d.bounded     = true;
    d.mean        = sum / d.samples;
    d.rms         = std::sqrt(sum2 / d.samples);
    metric(std::string(sign > 0. ? "convex_bead_" : "concave_bead_") + std::to_string(head), d);
    CHECK(d.maximum <= d.upper_bound);
    CHECK(d.within(.01));
}
TEST_CASE("An analytic hydrofoil has a measured and bounded polyline error", "[FormorixFoundations]")
{
    const auto curve = foil_curve();
    const auto line  = FG::discretize(curve, 0., 1., .001);
    REQUIRE(line.bounded);
    FG::Deviation d;
    double sum = 0., sum2 = 0.;
    for (std::size_t i = 1; i < line.samples.size(); ++i)
        for (int j = 0; j <= 40; ++j) {
            double t  = line.samples[i - 1].parameter + (line.samples[i].parameter - line.samples[i - 1].parameter) * j / 40.;
            double e  = distance_to_segment(curve.point(t), line.samples[i - 1].point, line.samples[i].point);
            d.maximum = std::max(d.maximum, e);
            sum += e;
            sum2 += e * e;
            ++d.samples;
        }
    d.mean        = sum / d.samples;
    d.rms         = std::sqrt(sum2 / d.samples);
    d.upper_bound = line.interpolation_bound;
    d.bounded     = line.bounded;
    metric("NACA0012_CAD_polyline", d);
    CHECK(d.within(.01));
    // Full upper/lower circumferential topology, with shared leading/trailing edges.
    FG::Mesh skin;
    std::vector<Vec3d> rim;
    for (const auto& s : line.samples)
        rim.push_back(s.point);
    for (std::size_t i = line.samples.size() - 2; i > 0; --i) {
        Vec3d q = line.samples[i].point;
        q.z()   = -q.z();
        rim.push_back(q);
    }
    for (const auto& q : rim) {
        skin.vertices.push_back(q);
        skin.vertices.push_back(q + Vec3d(0., 2., 0.));
    }
    for (std::size_t i = 0; i < rim.size(); ++i) {
        int a = int(2 * i), b = int(2 * ((i + 1) % rim.size()));
        skin.faces.emplace_back(a, a + 1, b);
        skin.faces.emplace_back(b, a + 1, b + 1);
    }
    const auto atlas = FG::make_atlas(skin);
    CHECK(atlas.boundary_edges == rim.size() * 2);
    CHECK(atlas.portals.size() > rim.size());
    const auto limited = FG::discretize(curve, 0., 1., .000001, 2);
    CHECK_FALSE(limited.bounded);
}
TEST_CASE("Chart seams and holes preserve continuity without an extrusion restart", "[FormorixFoundations]")
{
    auto patch = plane_patch(0., 1., 0., 1., 0.);
    auto atlas = FG::make_atlas(patch);
    REQUIRE(atlas.portals.size() == 1);
    const Vec3d shared(.5, .5, 0.);
    std::vector<FG::PathPiece> pieces = {{0, 2, "PETG-CF", {{.7, .2, 0.}, shared}}, {1, 2, "PETG-CF", {shared, {.2, .7, 0.}}}};
    auto route                        = FG::join_across_charts(patch, atlas, pieces);
    CHECK(route.size() == 3);
    CHECK((route[1] - shared).norm() < 1e-12);
    CHECK((FG::lift(patch, 0, {0., .5}) - shared).norm() < 1e-12);
    pieces[1].head = 0;
    CHECK_THROWS(FG::join_across_charts(patch, atlas, pieces));
    pieces[1].head = 2;
    pieces[1].points[0].x() += .01;
    CHECK_THROWS(FG::join_across_charts(patch, atlas, pieces));
    auto [solid, r]    = cylinder_solid(3., 16, .2);
    const auto wrapped = FG::make_atlas(r.surface);
    CHECK(wrapped.portals.size() == 32); // includes circumferential seam; no height-field restriction
    // Square annulus: topological hole remains a hole, not a false connecting seam.
    FG::Mesh annulus = {{{0., 0., 5.}, {3., 0., 5.}, {3., 3., 5.}, {0., 3., 5.}, {1., 1., 5.}, {2., 1., 5.}, {2., 2., 5.}, {1., 2., 5.}},
                        {{0, 1, 5}, {0, 5, 4}, {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}}};
    CHECK(FG::make_atlas(annulus).boundary_edges == 8);
    auto partition = FG::partition(box(0., 3., 0., 3.), {request(annulus, 1.)});
    INFO(partition.error);
    REQUIRE(partition.ok());
    CHECK_THAT(partition.parts[0].volume, WithinAbs(8., 1e-10));
    // A thin but valid triangle must not turn barycentric checks into 0/0.
    FG::Mesh thin         = {{{0., 0., 0.}, {1., 0., 0.}, {1., 1e-8, 0.}}, {{0, 1, 2}}};
    const auto thin_atlas = FG::make_atlas(thin);
    CHECK_THROWS(FG::join_across_charts(thin, thin_atlas, {{0, 0, "PETG", {{2., 0., 0.}, {3., 0., 0.}}}}));
    CHECK(FG::join_across_charts(thin, thin_atlas, {{0, 0, "PETG", {{.3, 1e-9, 0.}, {.6, 2e-9, 0.}}}}).size() == 2);
}
TEST_CASE("Three independent material profiles survive project migration and simultaneous time sampling", "[FormorixFoundations]")
{
    FS::Project project;
    PV::Timeline timeline;
    for (int h = 0; h < 3; ++h) {
        auto p                           = profile(h);
        project.heads[h].material_preset = "Orca preset " + p.material_id;
        project.heads[h].process         = p;
        PV::Pose a{{0., double(h), 0.}, Vec3d::UnitZ()}, b{{100., double(h), 0.}, Vec3d::UnitZ()};
        timeline.heads[h].push_back(PV::process_motion(h, h + 1, p, a, b, 0.));
    }
    REQUIRE(timeline.valid());
    std::string error;
    auto loaded = FS::Project::decode(project.encode(), error);
    REQUIRE(loaded);
    for (int h = 0; h < 3; ++h) {
        REQUIRE(loaded->heads[h].process);
        CHECK(loaded->heads[h].process->material_id == profile(h).material_id);
        CHECK_THAT(loaded->heads[h].process->nozzle_diameter, WithinAbs(profile(h).nozzle_diameter, 1e-12));
        auto state = timeline.sample(h, .1);
        REQUIRE(state.process);
        CHECK(state.process->material_id == profile(h).material_id);
        CHECK_THAT(state.pose.position.x(), WithinAbs(.1 * FP::effective_speed(profile(h)), 1e-9));
        CHECK(state.extrusion);
    }
    auto old       = nlohmann::json::parse(FS::Project{}.encode());
    old["version"] = 1;
    auto migrated  = FS::Project::decode(old.dump(), error);
    REQUIRE(migrated);
    CHECK(migrated->version == FS::schema_version);
    CHECK_FALSE(migrated->heads[0].process);
    old["version"] = 99;
    CHECK_FALSE(FS::Project::decode(old.dump(), error));
    auto invalid                         = project;
    invalid.heads[0].process->line_width = -1.;
    CHECK_THROWS(invalid.encode());
    project.heads[0].assign_material(project.heads[0].material_preset);
    CHECK(project.heads[0].process.has_value());
    project.heads[0].assign_material("different Orca material");
    CHECK_FALSE(project.heads[0].process);
    CHECK(project.heads[1].process.has_value());
}
TEST_CASE("Orca material and nozzle indices resolve independently", "[FormorixFoundations]")
{
    auto cfg = DynamicPrintConfig::full_print_config();
    cfg.set_key_value("nozzle_diameter", new ConfigOptionFloats{.8, .4, .6});
    cfg.set_key_value("nozzle_temperature", new ConfigOptionInts{240, 205, 250});
    cfg.set_key_value("filament_flow_ratio", new ConfigOptionFloats{1., .98, 1.02});
    cfg.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats{20., 4., 12.});
    cfg.set_key_value("filament_shrink", new ConfigOptionPercents{99., 100., 98.});
    cfg.set_key_value("filament_shrinkage_compensation_z", new ConfigOptionPercents{100., 100., 99.});
    auto p = FP::from_orca(cfg, 2, 1, "PVA-id", "custom-0.6", .66, .2, 40.);
    CHECK_THAT(p.nozzle_diameter, WithinAbs(.6, 1e-12));
    CHECK_THAT(p.temperature, WithinAbs(205., 1e-12));
    CHECK_THAT(p.volumetric_limit, WithinAbs(4., 1e-12));
    CHECK_THAT(p.flow_ratio, WithinAbs(.98, 1e-12));
    CHECK_THROWS(FP::from_orca(cfg, 3, 0, "PETG-id", "missing", .4, .2, 30.));
    p.layer_height = 2.;
    CHECK_FALSE(FP::valid(p));
    CHECK_THROWS(FP::section_area(p));
}
TEST_CASE("A surface requires completed compatible bounded underlay evidence", "[FormorixFoundations]")
{
    FP::Dependencies graph;
    graph.operations = {{1, 1, 0, "PETG", FP::OperationKind::Core},
                        {2, 1, 1, "PVA", FP::OperationKind::Soluble},
                        {3, 1, 2, "PETG-CF", FP::OperationKind::Surface}};
    graph.edges      = {{1, 2}, {2, 3}};
    CHECK(graph.valid());
    CHECK_FALSE(graph.prerequisites_complete(3, {2}));
    CHECK(graph.prerequisites_complete(3, {1, 2}));
    graph.edges.push_back({3, 1});
    CHECK_FALSE(graph.valid());
    FP::UnderlayEvidence support;
    support.allowed_gap     = .02;
    support.gap_upper_bound = .005;
    CHECK_FALSE(support.nominally_supported());
    support.finished         = true;
    support.coverage_bounded = true;
    CHECK_FALSE(support.nominally_supported());
    support.compatibility = FP::Compatibility::Compatible;
    CHECK(support.nominally_supported());
    support.gap_upper_bound = .03;
    CHECK_FALSE(support.nominally_supported());
}
TEST_CASE("Parameterized nozzle envelopes retain placeholder status and respect configured axes", "[FormorixFoundations]")
{
    FP::Machine machine;
    machine.heads[2].nozzle.cone_radius = 6.;
    auto a                              = FP::forward_envelope(machine, 0, {100., 100., 80.}, 90., 45., 20.);
    CHECK(a.placeholder);
    CHECK_THAT(a.axis.x(), WithinAbs(0., 1e-12));
    CHECK_THAT(a.axis.y(), WithinAbs(std::sqrt(.5), 1e-12));
    CHECK_THAT(a.tcp.z(), WithinAbs(60. - 35. * std::sqrt(.5), 1e-12));
    auto b = FP::forward_envelope(machine, 2, {100., 100., 80.}, 90., 45., 20.);
    CHECK(FP::bounds_may_overlap(a, b));
    b = FP::forward_envelope(machine, 2, {500., 100., 80.}, 90., 45., 20.);
    CHECK_FALSE(FP::bounds_may_overlap(a, b));
    CHECK(b.placeholder);
    CHECK_FALSE(FP::bounds_may_overlap(a, box()));
    CHECK(FP::bounds_may_overlap(a, box(80., 120., 80., 140., 100.)));
    CHECK_THROWS(FP::forward_envelope(machine, 1, {100., 100., 101.}, 0., 0., 0.));
    CHECK_THROWS(FP::forward_envelope(machine, 1, {100., 100., 80.}, 0., 76., 0.));
    CHECK_THROWS(FP::forward_envelope(machine, 1, {100., 100., 80.}, 0., 0., 351.));
}
