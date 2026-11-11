#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixSurfacePaths.hpp"
#include "libslic3r/FormorixGeometry.hpp"
#include "libslic3r/Point.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace Slic3r;
namespace SP = FormorixSurfacePaths;
using Catch::Matchers::WithinAbs;
namespace {
SP::Input plane()
{
    SP::Input input;
    input.surface.vertices = {{0., 0., 0.}, {10., 0., 2.}, {10., 2., 2.}, {0., 2., 0.}};
    input.surface.faces    = {{0, 1, 2}, {0, 2, 3}};
    input.vertex_normals.assign(4, Vec3d(-.2, 0., 1.).normalized());
    return input;
}
SP::Options options()
{
    SP::Options o;
    o.plane_normal            = Vec3d::UnitY();
    o.plane_offsets           = {1.};
    o.normal_offset_distance  = .4;
    o.interpolation_tolerance = .001;
    o.max_segment_length      = 2.;
    return o;
}
Vec3d interpolate(const SP::Input& input, const SP::Carrier& carrier, const std::array<double, 3>& bary, bool normal)
{
    Vec3d result     = Vec3d::Zero();
    const auto& face = input.surface.faces[carrier.face];
    for (int k = 0; k < 3; ++k)
        result += bary[k] * (normal ? input.vertex_normals[face[k]] : input.surface.vertices[face[k]]);
    return result;
}
void check_carriers(const SP::Input& input, const SP::Result& result, const SP::Options& o, const char* name)
{
    INFO(result.reason);
    REQUIRE(result.ok());
    REQUIRE_FALSE(result.runs.empty());
    REQUIRE(result.interpolation_bounded);
    CHECK(result.offset_interpolation_bound <= o.interpolation_tolerance);
    double maximum = 0., sum = 0., squares = 0.;
    std::size_t samples = 0, knots = 0;
    for (const auto& run : result.runs) {
        REQUIRE(run.knots.size() >= 2);
        REQUIRE(run.carriers.size() == run.knots.size() - (run.closed ? 0 : 1));
        REQUIRE(run.plane_index < o.plane_offsets.size());
        std::set<std::tuple<double, double, double>> positions;
        for (const auto& knot : run.knots) {
            CHECK(knot.position.allFinite());
            CHECK_THAT(knot.normal.norm(), WithinAbs(1., 1e-12));
            CHECK_THAT(knot.position.dot(o.plane_normal.normalized()), WithinAbs(o.plane_offsets[run.plane_index], 1e-10));
            CHECK(positions.emplace(knot.position.x(), knot.position.y(), knot.position.z()).second);
        }
        knots += run.knots.size();
        for (std::size_t i = 0; i < run.carriers.size(); ++i) {
            const auto& carrier = run.carriers[i];
            const auto& a       = run.knots[i];
            const auto& b       = run.knots[(i + 1) % run.knots.size()];
            REQUIRE(carrier.face >= 0);
            REQUIRE(std::size_t(carrier.face) < input.surface.faces.size());
            for (const auto* bary : {&carrier.from_barycentric, &carrier.to_barycentric}) {
                CHECK_THAT((*bary)[0] + (*bary)[1] + (*bary)[2], WithinAbs(1., 1e-12));
                for (double value : *bary) {
                    CHECK(std::isfinite(value));
                    CHECK(value >= 0.);
                    CHECK(value <= 1.);
                }
            }
            const auto pa = interpolate(input, carrier, carrier.from_barycentric, false);
            const auto pb = interpolate(input, carrier, carrier.to_barycentric, false);
            const auto na = interpolate(input, carrier, carrier.from_barycentric, true);
            const auto nb = interpolate(input, carrier, carrier.to_barycentric, true);
            CHECK((a.position - pa).norm() < 1e-10);
            CHECK((b.position - pb).norm() < 1e-10);
            CHECK((b.position - a.position).norm() <= o.max_segment_length + 1e-10);
            for (int sample = 0; sample <= 40; ++sample) {
                const double t        = sample / 40.;
                const Vec3d reference = (1. - t) * pa + t * pb - o.normal_offset_distance * ((1. - t) * na + t * nb).normalized();
                const Vec3d chord     = (1. - t) * (a.position - o.normal_offset_distance * a.normal) +
                                        t * (b.position - o.normal_offset_distance * b.normal);
                const double error    = (reference - chord).norm();
                CHECK(error <= result.offset_interpolation_bound + 2e-10);
                maximum = std::max(maximum, error);
                sum += error;
                squares += error * error;
                ++samples;
            }
        }
    }
    std::cout << std::setprecision(17) << "SURFACE_PATHS " << name << ",runs=" << result.runs.size() << ",knots=" << knots
              << ",point_rounding=" << result.position_rounding_bound << ",normal_rounding=" << result.normal_rounding_bound
              << ",offset_bound=" << result.offset_interpolation_bound << ",sample_max=" << maximum << ",sample_mean=" << sum / samples
              << ",sample_rms=" << std::sqrt(squares / samples) << '\n';
}
SP::Input extruded_loop(const std::vector<Vec2d>& loop)
{
    SP::Input input;
    const int count    = int(loop.size());
    double signed_area = 0.;
    for (int i = 0; i < count; ++i)
        signed_area += loop[i].x() * loop[(i + 1) % count].y() - loop[i].y() * loop[(i + 1) % count].x();
    for (int layer = 0; layer < 2; ++layer)
        for (int i = 0; i < count; ++i) {
            input.surface.vertices.emplace_back(loop[i].x(), 2. * layer, loop[i].y());
            const Vec2d before  = (loop[i] - loop[(i + count - 1) % count]).normalized();
            const Vec2d after   = (loop[(i + 1) % count] - loop[i]).normalized();
            const Vec2d tangent = before + after;
            Vec3d normal(tangent.y(), 0., -tangent.x());
            if (signed_area < 0.)
                normal = -normal;
            input.vertex_normals.push_back(normal.normalized());
        }
    for (int i = 0; i < count; ++i) {
        const int j = (i + 1) % count;
        if (signed_area > 0.) {
            input.surface.faces.emplace_back(i, i + count, j + count);
            input.surface.faces.emplace_back(i, j + count, j);
        } else {
            input.surface.faces.emplace_back(i, j, j + count);
            input.surface.faces.emplace_back(i, j + count, i + count);
        }
    }
    return input;
}
SP::Input cylinder()
{
    std::vector<Vec2d> loop;
    for (int i = 0; i < 64; ++i) {
        const double angle = 2. * std::acos(-1.) * i / 64.;
        loop.emplace_back(2. * std::cos(angle), 2. * std::sin(angle));
    }
    return extruded_loop(loop);
}
} // namespace

TEST_CASE("A planar surface contour crosses original triangle boundaries without an extrusion break", "[FormorixSurfacePaths]")
{
    const auto input  = plane();
    const auto o      = options();
    const auto result = SP::trace(input, o);
    check_carriers(input, result, o, "inclined_plane");
    REQUIRE(result.runs.size() == 1);
    CHECK_FALSE(result.runs[0].closed);
    std::set<int> faces;
    for (const auto& carrier : result.runs[0].carriers)
        faces.insert(carrier.face);
    CHECK(faces.size() == 2);
    CHECK_THAT((result.runs[0].knots.front().position - result.runs[0].knots.back().position).norm(), WithinAbs(std::sqrt(104.), 1e-12));
}

TEST_CASE("Doubly curved surface contours retain exact face carriers and a bounded normal offset interpolation", "[FormorixSurfacePaths]")
{
    SP::Input input;
    constexpr int count = 13;
    for (int j = 0; j < count; ++j)
        for (int i = 0; i < count; ++i) {
            const double x = -2. + i / 3., y = -2. + j / 3.;
            const double z = .3 * std::sin(x) * std::cos(y);
            input.surface.vertices.emplace_back(x, y, z);
            input.vertex_normals.push_back(Vec3d(-.3 * std::cos(x) * std::cos(y), .3 * std::sin(x) * std::sin(y), 1.).normalized());
        }
    for (int j = 0; j + 1 < count; ++j)
        for (int i = 0; i + 1 < count; ++i) {
            const int a = j * count + i, b = a + 1, c = b + count, d = a + count;
            input.surface.faces.emplace_back(a, b, c);
            input.surface.faces.emplace_back(a, c, d);
        }
    auto o                    = options();
    o.plane_offsets           = {-.7, .1, .8};
    o.normal_offset_distance  = .8;
    o.interpolation_tolerance = .0002;
    const auto result         = SP::trace(input, o);
    check_carriers(input, result, o, "double_curvature");
    CHECK(result.runs.size() == 3);
    for (const auto& run : result.runs)
        CHECK_FALSE(run.closed);
}

TEST_CASE("A polygon hole produces separate open extrusion runs instead of a connecting chord", "[FormorixSurfacePaths]")
{
    SP::Input input;
    input.surface.vertices = {{-4., -4., 0.}, {4., -4., 0.}, {4., 4., 0.}, {-4., 4., 0.},
                              {-1., -1., 0.}, {1., -1., 0.}, {1., 1., 0.}, {-1., 1., 0.}};
    input.vertex_normals.assign(8, Vec3d::UnitZ());
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        input.surface.faces.emplace_back(i, j, j + 4);
        input.surface.faces.emplace_back(i, j + 4, i + 4);
    }
    auto o            = options();
    o.plane_offsets   = {0.};
    const auto result = SP::trace(input, o);
    check_carriers(input, result, o, "polygon_hole");
    REQUIRE(result.runs.size() == 2);
    double length = 0.;
    for (const auto& run : result.runs) {
        CHECK_FALSE(run.closed);
        const bool left = run.knots.front().position.x() < 0.;
        for (const auto& knot : run.knots) {
            const bool outside_hole = left ? knot.position.x() <= -1. : knot.position.x() >= 1.;
            CHECK(outside_hole);
        }
        for (std::size_t i = 1; i < run.knots.size(); ++i)
            length += (run.knots[i].position - run.knots[i - 1].position).norm();
    }
    CHECK_THAT(length, WithinAbs(6., 1e-12));
}

TEST_CASE("A wrapped cylinder produces unique closed contours around every side without a height field", "[FormorixSurfacePaths]")
{
    const auto input         = cylinder();
    auto o                   = options();
    o.plane_offsets          = {.5, 1.5};
    o.normal_offset_distance = .3;
    const auto result        = SP::trace(input, o);
    check_carriers(input, result, o, "wrapped_cylinder");
    REQUIRE(result.runs.size() == 2);
    for (const auto& run : result.runs) {
        CHECK(run.closed);
        CHECK((run.knots.front().position - run.knots.back().position).norm() > 0.);
        double min_x = INFINITY, max_x = -INFINITY, min_z = INFINITY, max_z = -INFINITY;
        for (const auto& knot : run.knots) {
            min_x = std::min(min_x, knot.position.x());
            max_x = std::max(max_x, knot.position.x());
            min_z = std::min(min_z, knot.position.z());
            max_z = std::max(max_z, knot.position.z());
        }
        CHECK_THAT(min_x, WithinAbs(-2., 1e-12));
        CHECK_THAT(max_x, WithinAbs(2., 1e-12));
        CHECK_THAT(min_z, WithinAbs(-2., 1e-12));
        CHECK_THAT(max_z, WithinAbs(2., 1e-12));
    }
}

TEST_CASE("A complete NACA foil contour includes both skins the nose and the closed trailing edge", "[FormorixSurfacePaths]")
{
    std::vector<Vec2d> loop;
    constexpr int half = 64;
    for (int i = 0; i < 2 * half; ++i) {
        const double angle = std::acos(-1.) * i / half;
        const double x     = (1. - std::cos(angle)) / 2.;
        double thickness   = 60. * (.2969 * std::sqrt(x) - .1260 * x - .3516 * x * x + .2843 * x * x * x - .1036 * x * x * x * x);
        if (i == 0 || i == half)
            thickness = 0.; // analytic closed NACA endpoint values
        loop.emplace_back(100. * x, i <= half ? thickness : -thickness);
    }
    const auto input         = extruded_loop(loop);
    auto o                   = options();
    o.normal_offset_distance = .05;
    const auto result        = SP::trace(input, o);
    check_carriers(input, result, o, "full_naca0012");
    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].closed);
    double min_x = INFINITY, max_x = -INFINITY, min_z = INFINITY, max_z = -INFINITY;
    for (const auto& knot : result.runs[0].knots) {
        min_x = std::min(min_x, knot.position.x());
        max_x = std::max(max_x, knot.position.x());
        min_z = std::min(min_z, knot.position.z());
        max_z = std::max(max_z, knot.position.z());
    }
    CHECK_THAT(min_x, WithinAbs(0., 0.));
    CHECK_THAT(max_x, WithinAbs(100., 0.));
    CHECK(min_z < -5.9);
    CHECK(max_z > 5.9);
}

TEST_CASE("Tighter offset interpolation tolerances refine a contour without changing its source mesh", "[FormorixSurfacePaths]")
{
    const auto input                       = cylinder();
    const auto before                      = input.surface;
    auto coarse_options                    = options();
    coarse_options.normal_offset_distance  = 1.;
    coarse_options.interpolation_tolerance = .01;
    coarse_options.max_segment_length      = 10.;
    auto fine_options                      = coarse_options;
    fine_options.interpolation_tolerance   = .0001;
    const auto coarse = SP::trace(input, coarse_options), fine = SP::trace(input, fine_options);
    check_carriers(input, coarse, coarse_options, "coarse_offset");
    check_carriers(input, fine, fine_options, "fine_offset");
    CHECK(fine.runs[0].knots.size() > coarse.runs[0].knots.size());
    CHECK(fine.offset_interpolation_bound < coarse.offset_interpolation_bound);
    REQUIRE(input.surface.vertices.size() == before.vertices.size());
    REQUIRE(input.surface.faces.size() == before.faces.size());
    for (std::size_t i = 0; i < before.vertices.size(); ++i)
        CHECK_THAT((input.surface.vertices[i] - before.vertices[i]).norm(), WithinAbs(0., 0.));
}

TEST_CASE("Undefined and nearly vanishing normal fields never receive an interpolation certificate", "[FormorixSurfacePaths]")
{
    auto input = plane();
    for (std::size_t i = 0; i < input.surface.vertices.size(); ++i)
        input.vertex_normals[i] = Vec3d(input.surface.vertices[i].x() < 5. ? -1. : 1., 0., 0.);
    auto o                   = options();
    o.plane_offsets          = {.7};
    o.max_segment_length     = 100.;
    o.normal_offset_distance = 0.;
    const auto singular      = SP::trace(input, o);
    CHECK(singular.status == SP::Status::InvalidInput);
    CHECK(singular.runs.empty());
    CHECK_FALSE(singular.interpolation_bounded);
    for (auto& n : input.vertex_normals)
        n.z() = 1e-9;
    o.normal_offset_distance  = 1.;
    o.interpolation_tolerance = .000001;
    o.max_knots               = 16;
    const auto limited        = SP::trace(input, o);
    CHECK(limited.status == SP::Status::ResourceLimit);
    CHECK(limited.runs.empty());
    CHECK_FALSE(limited.interpolation_bounded);
}

TEST_CASE("Coincident vertices require one consistent normal field while uniform normal scaling remains valid", "[FormorixSurfacePaths]")
{
    auto input = plane();
    for (auto& n : input.vertex_normals)
        n *= 7.;
    const auto result = SP::trace(input, options());
    check_carriers(input, result, options(), "scaled_normals");
    input.surface.vertices.push_back(input.surface.vertices[0]);
    input.vertex_normals.push_back(Vec3d::UnitX());
    input.surface.faces[1][0] = 4;
    const auto inconsistent   = SP::trace(input, options());
    CHECK(inconsistent.status == SP::Status::InvalidInput);
    CHECK(inconsistent.runs.empty());
}

TEST_CASE("Empty and coplanar cuts cannot masquerade as complete surface area coverage", "[FormorixSurfacePaths]")
{
    auto input       = plane();
    auto o           = options();
    o.plane_offsets  = {5.};
    const auto empty = SP::trace(input, o);
    REQUIRE(empty.ok());
    CHECK(empty.runs.empty());
    CHECK_FALSE(empty.interpolation_bounded);
    CHECK(std::isinf(empty.offset_interpolation_bound));
    CHECK_FALSE(empty.reason.empty());
    for (auto& p : input.surface.vertices)
        p.z() = 0.;
    input.vertex_normals.assign(4, Vec3d::UnitZ());
    o.plane_normal      = Vec3d::UnitZ();
    o.plane_offsets     = {0.};
    const auto coplanar = SP::trace(input, o);
    REQUIRE(coplanar.ok());
    if (!coplanar.runs.empty())
        CHECK_FALSE(coplanar.warnings.empty());
}

TEST_CASE("Surface contour budgets cancellation and invalid inputs clear all positive evidence", "[FormorixSurfacePaths]")
{
    const auto input         = plane();
    auto o                   = options();
    o.max_carrier_candidates = 1;
    CHECK(SP::trace(input, o).status == SP::Status::ResourceLimit);
    o = options();
    o.plane_offsets.push_back(o.plane_offsets.front());
    CHECK(SP::trace(input, o).status == SP::Status::InvalidInput);
    o                  = options();
    o.plane_normal.x() = std::numeric_limits<double>::quiet_NaN();
    CHECK(SP::trace(input, o).status == SP::Status::InvalidInput);
    CHECK(SP::trace(input, options(), [] { return true; }).status == SP::Status::Canceled);
    int calls           = 0;
    const auto canceled = SP::trace(cylinder(), options(), [&] { return ++calls > 20; });
    CHECK(canceled.status == SP::Status::Canceled);
    CHECK(canceled.runs.empty());
    CHECK_FALSE(canceled.interpolation_bounded);
    auto bad                = input;
    bad.surface.faces[0][0] = -1;
    CHECK(SP::trace(bad, options()).status == SP::Status::InvalidInput);
}
