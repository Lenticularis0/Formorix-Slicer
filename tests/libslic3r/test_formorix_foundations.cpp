#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixGeometry.hpp"
#include "libslic3r/FormorixHybrid.hpp"
#include "libslic3r/FormorixProcess.hpp"
#include "libslic3r/FormorixDeposition.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/FormorixPreview.hpp"
#include "libslic3r/FormorixSurfaces.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Format/ProjectStrategy.hpp"
#include "libslic3r/Point.hpp"
#include "test_utils.hpp"
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
TEST_CASE("Representable planar interfaces retain double precision without unnecessary output quantization", "[FormorixFoundations]")
{
    const double x = GENERATE(0., 1590.);
    const auto source = box(x, x + 10.);
    const auto result = FG::partition(source, {request(plane_patch(x, x + 10., 0., 10., 5.), .4)});
    INFO(result.error);
    REQUIRE(result.ok());
    REQUIRE(result.interfaces.size() == 1);
    CHECK(result.output_topology_preserved);
    // Tangential Boolean intersection vertices at 1600 mm can round by an ULP;
    // the common horizontal interface must still retain its exact Z plane.
    CHECK(result.rounding_bound < 1e-12);
    for (const auto& p : result.interfaces.front().mesh.vertices)
        CHECK_THAT(p.z(), WithinAbs(5. - .4, 1e-14));
    CHECK_THAT(result.volume_residual, WithinAbs(0., 1e-10));
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
TEST_CASE("Sharp adjacent skins share a symmetric miter without changing the outside contour", "[FormorixFoundations]")
{
    const double top_depth   = GENERATE(1., .6);
    const double front_depth = 2. * top_depth;
    const auto original      = box();
    auto top                 = request(plane_patch(0., 10., 0., 10., 5.), top_depth, 7);
    auto front = request({{{0., 0., 0.}, {10., 0., 0.}, {10., 0., 5.}, {0., 0., 5.}}, {{0, 1, 2}, {0, 2, 3}}}, front_depth, 11);
    front.outward_normals.assign(4, -Vec3d::UnitY());
    CHECK_FALSE(FG::partition_clipped(original, {top, front}).ok());
    const auto result = FG::partition_clipped(original, {top, front}, FG::SharedBoundaryPolicy::MiterAdjacentEdges);
    INFO(result.error);
    REQUIRE(result.ok());
    REQUIRE(result.parts.size() == 3);
    CHECK(result.mitered_edges == 1);
    CHECK(result.mitered_vertices == 2);
    CHECK(result.miter_constraint_residual < 1e-12);
    auto part_volume = [](const auto& partition, int region) {
        for (const auto& part : partition.parts)
            if (part.region == region)
                return part.volume;
        return -1.;
    };
    CHECK_THAT(part_volume(result, 7), WithinAbs(10. * top_depth * (10. - front_depth / 2.), 1e-7));
    CHECK_THAT(part_volume(result, 11), WithinAbs(10. * front_depth * (5. - top_depth / 2.), 1e-7));
    CHECK_THAT(part_volume(result, 0), WithinAbs(10. * (10. - front_depth) * (5. - top_depth), 1e-7));
    CHECK_THAT(result.volume_residual, WithinAbs(0., 1e-7));
    bool material_boundary = false;
    for (const auto& interface : result.interfaces)
        if (interface.first == 7 && interface.second == 11) {
            material_boundary = true;
            const auto error  = FG::measure(interface.mesh, FG::plane({0., 0., 5.}, Vec3d(0., top_depth, front_depth)));
            CHECK(error.within(.000001));
        }
    REQUIRE(material_boundary); // region 7/11 may bind incompatible materials; never blended
    const auto reverse = FG::partition_clipped(original, {front, top}, FG::SharedBoundaryPolicy::MiterAdjacentEdges);
    INFO(reverse.error);
    REQUIRE(reverse.ok());
    for (const auto& part : result.parts) {
        CHECK(FG::valid_solid(part.mesh));
        CHECK_THAT(part_volume(reverse, part.region), WithinAbs(part.volume, 1e-9));
    }
    // Every exterior plane retains its complete original area, including the
    // sharp top/front corner. Shared internal faces do not count in this sum.
    for (int axis = 0; axis < 3; ++axis)
        for (int side = 0; side < 2; ++side) {
            const double coordinate = side ? (axis == 2 ? 5. : 10.) : 0.;
            double area             = 0.;
            for (const auto& part : result.parts)
                for (const auto& face : part.mesh.faces) {
                    const auto& a = part.mesh.vertices[face[0]];
                    const auto& b = part.mesh.vertices[face[1]];
                    const auto& c = part.mesh.vertices[face[2]];
                    if (std::abs(a[axis] - coordinate) < 1e-12 && std::abs(b[axis] - coordinate) < 1e-12 &&
                        std::abs(c[axis] - coordinate) < 1e-12)
                        area += (b - a).cross(c - a).norm() / 2.;
                }
            CHECK_THAT(area, WithinAbs(axis == 2 ? 100. : 50., 1e-7));
        }
    std::cout << "MITER sharp_box," << top_depth << "," << front_depth << "," << result.volume_residual << "," << result.rounding_bound
              << "," << result.miter_constraint_residual << "\n";
}

TEST_CASE("Mitering preserves flat depth steps and refuses unconnected overlap", "[FormorixFoundations]")
{
    auto first      = request(plane_patch(0., 5., 0., 10., 5.), 1., 1);
    auto second     = request(plane_patch(5., 10., 0., 10., 5.), 2., 2);
    const auto step = FG::partition_clipped(box(), {first, second}, FG::SharedBoundaryPolicy::MiterAdjacentEdges);
    INFO(step.error);
    REQUIRE(step.ok());
    CHECK(step.mitered_edges == 0);
    CHECK_THAT(step.volume_residual, WithinAbs(0., 1e-10));
    second = request(plane_patch(4., 9., 0., 10., 5.), 1., 2);
    CHECK_FALSE(FG::partition_clipped(box(), {first, second}, FG::SharedBoundaryPolicy::MiterAdjacentEdges).ok());
    second        = first;
    second.region = 2;
    CHECK_FALSE(FG::partition_clipped(box(), {first, second}, FG::SharedBoundaryPolicy::MiterAdjacentEdges).ok());
}

TEST_CASE("A miter that crosses the first fine inner row is rejected without altering CAD input", "[FormorixFoundations]")
{
    const auto solid          = box();
    const auto original       = solid;
    FG::Mesh fine             = {{{0., 0., 5.}, {10., 0., 5.}, {0., .1, 5.}, {10., .1, 5.}, {0., 10., 5.}, {10., 10., 5.}},
                                 {{0, 1, 2}, {1, 3, 2}, {2, 3, 4}, {3, 5, 4}}};
    const auto surface_before = fine;
    auto top                  = request(fine, .6, 1);
    auto front                = request({{{0., 0., 0.}, {10., 0., 0.}, {10., 0., 5.}, {0., 0., 5.}}, {{0, 1, 2}, {0, 2, 3}}}, 1., 2);
    front.outward_normals.assign(4, -Vec3d::UnitY());
    const auto result = FG::partition_clipped(solid, {top, front}, FG::SharedBoundaryPolicy::MiterAdjacentEdges);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error.find("Folded offset") != std::string::npos);
    CHECK(result.parts.empty());
    CHECK(result.interfaces.empty());
    CHECK(solid.vertices == original.vertices);
    CHECK(solid.faces == original.faces);
    CHECK(top.surface.vertices == surface_before.vertices);
    CHECK(top.surface.faces == surface_before.faces);
    // The continuous miter exists, but relocating its inner boundary past
    // y=.1 would invert the fixed triangulation. No silent CAD simplification.
}

TEST_CASE("A curved common edge keeps one disjoint miter with independent varying normals", "[FormorixFoundations]")
{
    constexpr int count = 5;
    const std::array<double, count + 1> heights{{5., 6., 6.5, 6.5, 6., 5.}};
    FG::Mesh solid, roof, front;
    for (int i = 0; i <= count; ++i)
        for (double y : {0., 10.})
            roof.vertices.emplace_back(2. * i, y, heights[i]);
    solid.vertices = roof.vertices;
    const int base = int(solid.vertices.size());
    for (int i = 0; i <= count; ++i)
        for (double y : {0., 10.})
            solid.vertices.emplace_back(2. * i, y, 0.);
    for (int i = 0; i < count; ++i) {
        const int a = 2 * i, b = a + 2;
        roof.faces.emplace_back(a, b, a + 1);
        roof.faces.emplace_back(b, b + 1, a + 1);
        solid.faces.emplace_back(a + base, a + 1 + base, b + base);
        solid.faces.emplace_back(b + base, a + 1 + base, b + 1 + base);
        solid.faces.emplace_back(a + base, b + base, b);
        solid.faces.emplace_back(a + base, b, a);
        solid.faces.emplace_back(a + 1 + base, a + 1, b + 1);
        solid.faces.emplace_back(a + 1 + base, b + 1, b + 1 + base);
        front.faces.emplace_back(a, b, b + 1);
        front.faces.emplace_back(a, b + 1, a + 1);
    }
    for (int i = 0; i <= count; ++i) {
        front.vertices.emplace_back(2. * i, 0., 0.);
        front.vertices.emplace_back(2. * i, 0., heights[i]);
    }
    solid.faces.insert(solid.faces.end(), roof.faces.begin(), roof.faces.end());
    solid.faces.emplace_back(base, 0, 1);
    solid.faces.emplace_back(base, 1, base + 1);
    const int end = 2 * count;
    solid.faces.emplace_back(base + end, base + end + 1, end + 1);
    solid.faces.emplace_back(base + end, end + 1, end);
    REQUIRE(FG::valid_solid(solid));
    auto top_request   = request(roof, .4, 31);
    auto front_request = request(front, .7, 47);
    front_request.outward_normals.assign(front.vertices.size(), -Vec3d::UnitY());
    for (int i = 0; i <= count; ++i) {
        const int left = std::max(0, i - 1), right = std::min(count, i + 1);
        const Vec3d normal                     = Vec3d(-(heights[right] - heights[left]) / (2. * (right - left)), 0., 1.).normalized();
        top_request.outward_normals[2 * i]     = normal;
        top_request.outward_normals[2 * i + 1] = normal;
    }
    const auto original_normals = top_request.outward_normals;
    const auto result           = FG::partition_clipped(solid, {top_request, front_request}, FG::SharedBoundaryPolicy::MiterAdjacentEdges);
    INFO(result.error);
    REQUIRE(result.ok());
    CHECK(result.mitered_edges == count);
    CHECK(result.mitered_vertices == count + 1);
    CHECK(result.miter_constraint_residual < 1e-12);
    CHECK_THAT(result.volume_residual, WithinAbs(0., 1e-6));
    CHECK(top_request.outward_normals == original_normals);
    CHECK(std::any_of(result.interfaces.begin(), result.interfaces.end(), [](const auto& x) { return x.first == 31 && x.second == 47; }));
    const auto reverse = FG::partition_clipped(solid, {front_request, top_request}, FG::SharedBoundaryPolicy::MiterAdjacentEdges);
    INFO(reverse.error);
    REQUIRE(reverse.ok());
    for (const auto& part : result.parts) {
        CHECK(FG::valid_solid(part.mesh));
        const auto match = std::find_if(reverse.parts.begin(), reverse.parts.end(), [&](const auto& p) { return p.region == part.region; });
        REQUIRE(match != reverse.parts.end());
        CHECK_THAT(match->volume, WithinAbs(part.volume, 1e-7));
    }
    std::cout << "MITER curved_edge," << result.volume_residual << "," << result.rounding_bound << "," << result.miter_constraint_residual
              << "\n";
}

TEST_CASE("Both saved curved-wall markings have disjoint geometry before nozzle reachability is considered", "[FormorixFoundations]")
{
    ScopedSlic3rTemporaryDir temporary("formorix-miter-saved-regions");
    DynamicPrintConfig config;
    ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Disable);
    Model model = Model::read_from_file(std::string(TEST_DATA_DIR) + "/../../docs/formorix/examples/curved-wall.3mf", &config,
                                        &substitutions,
                                        LoadStrategy::LoadModel | LoadStrategy::LoadConfig | LoadStrategy::AddDefaultInstances);
    std::string error;
    auto project = FS::Project::decode(model.formorix_surfaces, error);
    INFO(error);
    REQUIRE(project);
    REQUIRE_FALSE(model.objects.empty());
    const auto input = FormorixHybrid::extract(*model.objects.front(), 0, *project, {profile(0), profile(1), profile(2)});
    REQUIRE(input.regions.size() == 2);
    std::vector<FG::ShellRequest> requests;
    for (const auto& region : input.regions) {
        REQUIRE(region.settings.shell_mode == FS::ShellMode::Layers);
        REQUIRE(region.settings.fine_final_layers == 0);
        auto r = request(region.surface, region.settings.layers * region.settings.layer_height, region.id);
        r.outward_normals.assign(r.surface.vertices.size(), Vec3d::Zero());
        for (const auto& f : r.surface.faces) {
            const Vec3d n = (r.surface.vertices[f[1]] - r.surface.vertices[f[0]]).cross(r.surface.vertices[f[2]] - r.surface.vertices[f[0]]);
            for (int k = 0; k < 3; ++k)
                r.outward_normals[f[k]] += n;
        }
        for (auto& n : r.outward_normals)
            n.normalize();
        requests.push_back(std::move(r));
    }
    const auto result = FG::partition_clipped(input.solid, requests, FG::SharedBoundaryPolicy::MiterAdjacentEdges);
    INFO(result.error);
    REQUIRE(result.ok());
    REQUIRE(result.parts.size() == 3);
    CHECK(result.mitered_edges > 0);
    CHECK(result.miter_constraint_residual < 1e-8);
    CHECK_THAT(result.volume_residual, WithinAbs(0., 1e-5));
    CHECK(std::any_of(result.interfaces.begin(), result.interfaces.end(), [](const auto& x) { return x.first == 1 && x.second == 2; }));
    for (const auto& part : result.parts)
        CHECK(FG::valid_solid(part.mesh));
    std::reverse(requests.begin(), requests.end());
    const auto reversed = FG::partition_clipped(input.solid, requests, FG::SharedBoundaryPolicy::MiterAdjacentEdges);
    INFO(reversed.error);
    REQUIRE(reversed.ok());
    for (const auto& part : result.parts) {
        const auto match = std::find_if(reversed.parts.begin(), reversed.parts.end(),
                                        [&](const auto& p) { return p.region == part.region; });
        REQUIRE(match != reversed.parts.end());
        CHECK_THAT(match->volume, WithinAbs(part.volume, 1e-5));
    }
    std::cout << "MITER original_curved_wall_both_regions," << std::setprecision(17) << result.input_volume << "," << result.volume_residual
              << "," << result.rounding_bound << "," << result.miter_constraint_residual << "," << result.mitered_edges << "\n";
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
    // This fixture requires the common-grid fallback. Its small vertex rounding
    // does not prove that topological repair preserves the complete boundary.
    CHECK_FALSE(result.output_topology_preserved);
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

TEST_CASE("Compacted nominal ribbons retain Orca supplied volume and independent head dimensions", "[FormorixFoundations][RibbonModel]")
{
    const int head                = GENERATE(0, 1, 2);
    const double flow             = GENERATE(.85, 1., 1.15);
    auto p                        = profile(head);
    p.flow_ratio                  = flow;
    const nlohmann::json original = p;
    const auto section            = FP::ribbon_section(p);
    const Flow orca(float(p.line_width), float(p.layer_height), float(p.nozzle_diameter));
    CHECK(section.model_version == 1);
    CHECK_THAT(section.requested_width, WithinAbs(p.line_width, 1e-14));
    CHECK_THAT(section.height, WithinAbs(p.layer_height, 1e-14));
    // Orca Flow deliberately stores these dimensions/results in float; the
    // new nominal model must agree without inheriting that precision loss.
    CHECK_THAT(section.area_before_flow, WithinAbs(orca.mm3_per_mm(), 2e-7 * orca.mm3_per_mm()));
    CHECK_THAT(section.supplied_area, WithinAbs(orca.mm3_per_mm() * flow, 2e-7 * orca.mm3_per_mm() * flow));
    CHECK_THAT(section.compacted_width * section.height, WithinAbs(section.supplied_area, 1e-14));
    CHECK(section.area_before_flow > FP::section_area(p));
    CHECK(nlohmann::json(p) == original);
}

TEST_CASE("Ribbon speed respects supplied stadium volume without changing the flow setting", "[FormorixFoundations][RibbonModel]")
{
    auto p             = profile(1);
    p.speed            = 500.;
    p.volumetric_limit = 2.;
    p.flow_ratio       = 1.12;
    const auto section = FP::ribbon_section(p);
    const double speed = FP::ribbon_speed(p);
    CHECK_THAT(speed * section.supplied_area, WithinAbs(p.volumetric_limit, 1e-12));
    CHECK(speed < FP::effective_speed(p)); // the legacy ellipse has a smaller section
    CHECK_THAT(p.flow_ratio, WithinAbs(1.12, 1e-14));
    p.speed = 1.;
    CHECK_THAT(FP::ribbon_speed(p), WithinAbs(1., 1e-14));
}

TEST_CASE("Ribbon sections expose calibrated dimensions and reject inverted or invalid geometry", "[FormorixFoundations][RibbonModel]")
{
    auto p            = profile(1);
    p.line_width      = .2;
    p.layer_height    = .2;
    const auto circle = FP::ribbon_section(p);
    CHECK_THAT(circle.area_before_flow, WithinAbs(pi * .1 * .1, 1e-14));
    CHECK_THAT(circle.compacted_width, WithinAbs(pi * .05, 1e-14));
    p                     = profile(1);
    p.width_correction    = 1.2;
    p.height_correction   = 1.1;
    const auto calibrated = FP::ribbon_section(p);
    const Flow orca(float(p.line_width * p.width_correction), float(p.layer_height * p.height_correction), float(p.nozzle_diameter));
    CHECK_THAT(calibrated.requested_width, WithinAbs(p.line_width, 1e-14));
    CHECK_THAT(calibrated.height, WithinAbs(p.layer_height * p.height_correction, 1e-14));
    CHECK_THAT(calibrated.area_before_flow, WithinAbs(orca.mm3_per_mm(), 2e-7 * orca.mm3_per_mm()));
    p.width_correction  = .5;
    p.height_correction = 2.;
    REQUIRE(FP::valid(p));
    CHECK_THROWS(FP::ribbon_section(p));
    p            = profile(1);
    p.flow_ratio = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS(FP::ribbon_section(p));
    CHECK_THROWS(FP::ribbon_speed(p));
    p              = profile(1);
    p.layer_height = 0.;
    CHECK_THROWS(FP::ribbon_section(p));
}

TEST_CASE("Finite mitered ribbon turns preserve a rectangular footprint and its supplied volume", "[FormorixFoundations][RibbonModel]")
{
    const int rows     = GENERATE(1, 2, 5);
    auto p             = profile(GENERATE(0, 1, 2));
    p.flow_ratio       = 1.08;
    const auto section = FP::ribbon_section(p);
    const double width = 8., pitch = section.compacted_width, depth = rows * pitch;
    std::vector<Vec2d> centers{{0., pitch / 2.}};
    for (int row = 0; row < rows; ++row) {
        const bool right = row % 2 == 0;
        const double end = row == rows - 1 ? (right ? width : 0.) : (right ? width - pitch / 2. : pitch / 2.);
        centers.emplace_back(end, (row + .5) * pitch);
        if (row < rows - 1)
            centers.emplace_back(end, (row + 1.5) * pitch);
    }
    double length = 0.;
    Polyline line;
    for (std::size_t i = 0; i < centers.size(); ++i) {
        line.points.emplace_back(Point::new_scale(centers[i].x(), centers[i].y()));
        if (i)
            length += (centers[i] - centers[i - 1]).norm();
    }
    // This finite reference includes every U-turn. Its connector length is
    // balanced geometrically by shorter straight rows, without changing flow.
    CHECK_THAT(length, WithinAbs(rows * width, 1e-12));
    CHECK_THAT(length * section.supplied_area, WithinAbs(width * depth * section.height, 1e-12));
    const Polygons target{Polygon::new_scale({{0., 0.}, {width, 0.}, {width, depth}, {0., depth}})};
    const auto swept = offset(line, float(scale_(pitch / 2.)), jtMiter, 2., etOpenButt);
    const auto area  = [](const Polygons& polygons) {
        double sum = 0.;
        for (const auto& polygon : polygons)
            sum += polygon.area();
        return std::abs(sum) * SCALING_FACTOR * SCALING_FACTOR;
    };
    // Clipper's scaled integer vertices/float radius are independently audited;
    // the tolerance is an area allowance for their micrometre-scale rounding.
    const double rounding_area = width * rows * 1e-5;
    CHECK_THAT(area(diff(target, swept)), WithinAbs(0., rounding_area));
    CHECK_THAT(area(diff(swept, target)), WithinAbs(0., rounding_area));
    CHECK_THAT(area(swept) * section.height, WithinAbs(length * section.supplied_area, rounding_area * section.height));
}

TEST_CASE("Ribbon volume ledgers distinguish numerical residue from inadmissible and gross material excess",
          "[FormorixFoundations][RibbonModel]")
{
    const auto original_ellipse = FP::ribbon_balance(4642.359, 1000.);
    CHECK_THAT(original_ellipse.excess_volume, WithinAbs(3642.359, 1e-9));
    CHECK(original_ellipse.inadmissible);
    CHECK(original_ellipse.gross_oversupply);
    const auto small_excess = FP::ribbon_balance(1002., 1000.);
    CHECK_THAT(small_excess.excess_volume, WithinAbs(2., 1e-12));
    CHECK(small_excess.inadmissible);
    CHECK_FALSE(small_excess.gross_oversupply);
    const auto gross = FP::ribbon_balance(1100.1, 1000.);
    CHECK(gross.gross_oversupply);
    const auto deficient = FP::ribbon_balance(900., 1000.);
    CHECK_THAT(deficient.excess_volume, WithinAbs(0., 1e-12));
    CHECK_FALSE(deficient.inadmissible);
    CHECK_FALSE(deficient.gross_oversupply);
    // No positive-overflow classification is a proof of coverage or sufficient supply.
}

TEST_CASE("Ribbon numerical allowances are explicit signed residuals and never alter supplied material",
          "[FormorixFoundations][RibbonModel]")
{
    const double supplied = 10. + .5e-6, target = 10.;
    const auto positive = FP::ribbon_balance(supplied, target, 1e-6);
    const auto negative = FP::ribbon_balance(supplied, target, -1e-6);
    CHECK_THAT(positive.numerical_tolerance, WithinAbs(1.1e-6, 1e-16));
    CHECK_THAT(negative.numerical_tolerance, WithinAbs(positive.numerical_tolerance, 1e-16));
    CHECK_THAT(positive.excess_volume, WithinAbs(supplied - target, 1e-16));
    CHECK_FALSE(positive.inadmissible);
    CHECK_FALSE(negative.inadmissible);
    CHECK(FP::ribbon_balance(supplied, target).inadmissible);
    CHECK(FP::ribbon_balance(10. + 2e-6, target, 1e-6).inadmissible);
    CHECK_FALSE(FP::ribbon_balance(target, target).inadmissible);
}

TEST_CASE("Ribbon volume ledgers reject invalid quantities and nonfinite uncertainty", "[FormorixFoundations][RibbonModel]")
{
    CHECK_THROWS(FP::ribbon_balance(-1., 1.));
    CHECK_THROWS(FP::ribbon_balance(1., 0.));
    CHECK_THROWS(FP::ribbon_balance(1., -1.));
    const double invalid = GENERATE(std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN());
    CHECK_THROWS(FP::ribbon_balance(invalid, 1.));
    CHECK_THROWS(FP::ribbon_balance(1., invalid));
    CHECK_THROWS(FP::ribbon_balance(1., 1., invalid));
    const double maximum = std::numeric_limits<double>::max();
    CHECK_THROWS(FP::ribbon_balance(maximum, maximum, maximum));
}

TEST_CASE("Local ribbon widths require declared limits and preserve every configured flow component",
          "[FormorixFoundations][RibbonModel]")
{
    auto p = profile(0);
    p.line_width = .88;
    p.layer_height = .2;
    p.flow_ratio = .98;
    p.print_flow_ratio = 1.03;
    p.surface_flow_ratio = .97;
    p.speed = 500.;
    p.volumetric_limit = 8.;
    CHECK_THROWS(FP::ribbon_section(p, .9));
    p.local_width_limits = FP::WidthLimits{.8, 1.};
    const nlohmann::json before = p;
    for (double width : {.8, .9, 1.}) {
        const auto section = FP::ribbon_section(p, width);
        const double area = (.2 * (width - .2) + pi * .2 * .2 / 4.) * .98 * 1.03 * .97;
        CHECK_THAT(section.requested_width, WithinAbs(width, 1e-12));
        CHECK_THAT(section.supplied_area, WithinAbs(area, 1e-12));
        CHECK_THAT(section.compacted_width * section.height, WithinAbs(area, 1e-12));
        CHECK_THAT(FP::ribbon_speed(p, width), WithinAbs(8. / area, 1e-12));
    }
    CHECK(nlohmann::json(p) == before);
    CHECK_THROWS(FP::ribbon_section(p, .799));
    CHECK_THROWS(FP::ribbon_section(p, 1.001));
    CHECK_THROWS(FP::ribbon_section(p, std::numeric_limits<double>::quiet_NaN()));
    // A declared interval cannot waive the actual layer/corrected-section limits.
    p.line_width = .3;
    p.local_width_limits = FP::WidthLimits{.1, .5};
    CHECK_THROWS(FP::ribbon_section(p, .19));
    p.layer_height = .1;
    CHECK_NOTHROW(FP::ribbon_section(p, .19));
    p.surface_flow_ratio = .001; // Orca permits positive role factors below 0.01.
    CHECK_NOTHROW(FP::ribbon_section(p, .19));
    p.surface_flow_ratio = 0.;
    CHECK_THROWS(FP::ribbon_section(p, .19));
    p.surface_flow_ratio = std::numeric_limits<double>::denorm_min();
    p.flow_ratio = .1;
    p.print_flow_ratio = .01;
    CHECK_THROWS(FP::effective_surface_flow(p));
    CHECK_THROWS(FP::ribbon_section(p, .19));
    p.flow_ratio = p.print_flow_ratio = 1.;
    CHECK_THAT(FP::effective_surface_flow(p), WithinAbs(std::numeric_limits<double>::denorm_min(), 0.));
    CHECK_THROWS(FP::ribbon_section(p, .19));
    CHECK_THROWS(FP::ribbon_speed(p, .19));
}

TEST_CASE("Legacy process snapshots have neutral additional flow and no adaptive width authorization",
          "[FormorixFoundations][RibbonModel]")
{
    auto p = profile(1);
    p.local_width_limits = FP::WidthLimits{.3, .8};
    p.print_flow_ratio = 1.1;
    p.surface_flow_ratio = .9;
    auto data = nlohmann::json(p);
    const auto restored = data.get<FP::Profile>();
    REQUIRE(restored.local_width_limits);
    CHECK_THAT(restored.print_flow_ratio, WithinAbs(1.1, 1e-12));
    CHECK_THAT(restored.surface_flow_ratio, WithinAbs(.9, 1e-12));
    data.erase("local_width_limits");
    data.erase("print_flow_ratio");
    data.erase("surface_flow_ratio");
    const auto legacy = data.get<FP::Profile>();
    CHECK_FALSE(legacy.local_width_limits);
    CHECK_THAT(FP::effective_surface_flow(legacy), WithinAbs(legacy.flow_ratio, 1e-12));
    for (double bad : {0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid = legacy;
        invalid.print_flow_ratio = bad;
        CHECK_FALSE(FP::valid(invalid));
        CHECK_THROWS(FP::ribbon_section(invalid));
    }
}
