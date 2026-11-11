#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixBoundary.hpp"
#include "libslic3r/FormorixDeposition.hpp"
#include "libslic3r/FormorixGeometry.hpp"
#include "libslic3r/FormorixMaterial.hpp"
#include "libslic3r/FormorixProcess.hpp"
#include "libslic3r/Point.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
namespace FB = FormorixBoundary;
namespace FM = FormorixMaterial;
namespace FG = FormorixGeometry;
namespace PS = FormorixProcess;
using Catch::Matchers::WithinAbs;
namespace {
FB::Domain rectangle(double width = 12., double depth = 6.1)
{ return {{{0., 0.}, {width, 0.}, {width, depth}, {0., depth}}, {{0, 1, 2}, {0, 2, 3}}}; }
PS::Profile profile(bool adaptive = true)
{
    PS::Profile p;
    p.material_id      = "PETG";
    p.nozzle_profile   = "0.8 mm";
    p.nozzle_diameter  = .8;
    p.layer_height     = .2;
    p.line_width       = .75 + p.layer_height * (1. - std::acos(-1.) / 4.);
    p.speed            = 60.;
    p.volumetric_limit = 4.;
    if (adaptive)
        p.local_width_limits = PS::WidthLimits{.74, .84};
    return p;
}
FG::Mesh box(double width, double depth, double height)
{
    FG::Mesh m;
    m.vertices = {{0., 0., 0.},     {width, 0., 0.},     {width, depth, 0.},     {0., depth, 0.},
                  {0., 0., height}, {width, 0., height}, {width, depth, height}, {0., depth, height}};
    m.faces    = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                  {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
    return m;
}
FM::SweepRun sweep(const FB::Result& plan, double bottom, double top, int layer = 0)
{
    FM::SweepRun run;
    run.region          = 1;
    run.material        = plan.local_profile.material_id;
    run.layer           = layer;
    const double center = (bottom + top) / 2.;
    for (std::size_t i = 0; i < plan.path.size(); ++i) {
        const auto& p = plan.path[i];
        FM::Station station{{p.x(), p.y(), center}, Vec3d::UnitZ(), plan.section.compacted_width, plan.section.height};
        station.height_endpoints  = std::array<Vec3d, 2>{{{p.x(), p.y(), bottom}, {p.x(), p.y(), top}}};
        station.lateral_endpoints = std::array<Vec3d, 2>{{{plan.lateral_endpoints[i][0].x(), plan.lateral_endpoints[i][0].y(), center},
                                                          {plan.lateral_endpoints[i][1].x(), plan.lateral_endpoints[i][1].y(), center}}};
        run.stations.push_back(std::move(station));
        if (i)
            run.supplied_volumes.push_back((plan.path[i] - plan.path[i - 1]).norm() * plan.section.supplied_area);
    }
    return run;
}
void ready(const FB::Result& result)
{
    INFO(result.reason);
    REQUIRE(result.ok());
    REQUIRE(result.path.size() >= 2);
    REQUIRE(result.lateral_endpoints.size() == result.path.size());
}
FM::Result audit(const std::vector<FM::SweepRun>& runs, double width, double depth, double height, const Vec3d& origin = Vec3d::Zero())
{
    FM::Target target;
    target.region   = 1;
    target.material = "PETG";
    target.mesh     = box(width, depth, height);
    for (auto& vertex : target.mesh.vertices)
        vertex += origin;
    return FM::audit({target}, {}, runs);
}
void full(const FM::Result& evidence, double volume)
{
    INFO(evidence.error);
    REQUIRE(evidence.ok());
    REQUIRE(evidence.regions.size() == 1);
    CHECK(evidence.regions.front().coverage == FM::Coverage::Full);
    CHECK_THAT(evidence.union_volume, WithinAbs(volume, 1e-11));
    CHECK_THAT(evidence.supplied_volume, WithinAbs(volume, 1e-11));
    CHECK_THAT(evidence.missing_volume, WithinAbs(0., 0.));
    CHECK_THAT(evidence.outside_volume, WithinAbs(0., 0.));
    CHECK_THAT(evidence.duplicate_volume, WithinAbs(0., 0.));
    CHECK_THAT(evidence.core_overlap_volume, WithinAbs(0., 0.));
    CHECK(evidence.boundary_distance.bounded);
    CHECK(evidence.boundary_distance.upper_bound < .01);
}
} // namespace

TEST_CASE("Explicit local width limits fill a decimal rectangle with finite canonical miter rails", "[FormorixBoundary]")
{
    const auto p      = profile();
    const auto result = FB::plan(rectangle(), p);
    ready(result);
    REQUIRE(result.full_candidate);
    CHECK(result.rows == 8);
    CHECK(result.width_adjusted);
    CHECK_THAT(result.section.compacted_width, WithinAbs(.7625, 1e-14));
    CHECK(result.local_profile.line_width >= p.local_width_limits->minimum_mm);
    CHECK(result.local_profile.line_width <= p.local_width_limits->maximum_mm);
    CHECK_THAT(p.line_width, WithinAbs(.75 + .2 * (1. - std::acos(-1.) / 4.), 0.));
    CHECK_THAT(result.local_profile.flow_ratio, WithinAbs(p.flow_ratio, 0.));
    CHECK_THAT(result.supplied_volume, WithinAbs(12. * 6.1 * .2, 1e-11));
    CHECK(result.construction_bound < 1e-12);
    CHECK(result.remainder.empty());
    full(audit({sweep(result, 0., .2)}, 12., 6.1, .2), 14.64);
}

TEST_CASE("A commensurate fixed-width rectangle retains its original profile and complete finite volume", "[FormorixBoundary]")
{
    const auto p      = profile(false);
    const auto result = FB::plan(rectangle(12., 3.), p);
    ready(result);
    CHECK(result.full_candidate);
    CHECK_FALSE(result.width_adjusted);
    CHECK_THAT(result.local_profile.line_width, WithinAbs(p.line_width, 0.));
    full(audit({sweep(result, 0., .2)}, 12., 3., .2), 7.2);
}

TEST_CASE("A fixed profile leaves visible finite remainder strips instead of altering flow", "[FormorixBoundary]")
{
    const auto p      = profile(false);
    const auto result = FB::plan(rectangle(), p);
    ready(result);
    CHECK_FALSE(result.full_candidate);
    CHECK_FALSE(result.width_adjusted);
    CHECK(result.remainder.size() == 2);
    CHECK_THAT(result.residual_width, WithinAbs(.1, 1e-14));
    CHECK_THAT(result.local_profile.line_width, WithinAbs(p.line_width, 0.));
    CHECK_THAT(result.local_profile.flow_ratio, WithinAbs(p.flow_ratio, 0.));
    const auto evidence = audit({sweep(result, 0., .2)}, 12., 6.1, .2);
    INFO(evidence.error);
    REQUIRE(evidence.ok());
    CHECK(evidence.regions[0].coverage == FM::Coverage::Partial);
    CHECK_THAT(evidence.missing_volume, WithinAbs(12. * .1 * .2, 1e-11));
    CHECK_THAT(evidence.outside_volume, WithinAbs(0., 0.));
    CHECK_THAT(evidence.duplicate_volume, WithinAbs(0., 0.));
}

TEST_CASE("Each layer independently respects width limits and retains exact adjacent height interfaces", "[FormorixBoundary]")
{
    auto p           = profile();
    const auto lower = FB::plan(rectangle(), p);
    ready(lower);
    p.layer_height   = .1;
    const auto upper = FB::plan(rectangle(), p);
    ready(upper);
    CHECK(lower.full_candidate);
    CHECK(upper.full_candidate);
    CHECK(lower.local_profile.line_width > upper.local_profile.line_width);
    full(audit({sweep(lower, 0., .2), sweep(upper, .2, .3, 1)}, 12., 6.1, .3), 12. * 6.1 * .3);
}

TEST_CASE("Canonical lateral rails retain a shifted decimal rectangle across multiple layer heights", "[FormorixBoundary]")
{
    auto domain = rectangle();
    for (auto& point : domain.vertices)
        point += Vec2d(1588., -6.1);
    auto p           = profile();
    const auto lower = FB::plan(domain, p);
    ready(lower);
    p.layer_height   = .1;
    const auto upper = FB::plan(domain, p);
    ready(upper);
    CHECK(lower.full_candidate);
    CHECK(upper.full_candidate);
    full(audit({sweep(lower, 0., .2), sweep(upper, .2, .3, 1)}, 12., 6.1, .3, Vec3d(1588., -6.1, 0.)), 12. * 6.1 * .3);
}

TEST_CASE("Boundary adaptation preserves every configured flow factor and the actual volumetric speed limit", "[FormorixBoundary]")
{
    auto p               = profile();
    p.local_width_limits = PS::WidthLimits{.65, .95};
    p.flow_ratio         = .97;
    p.print_flow_ratio   = .95;
    p.surface_flow_ratio = 1.03;
    const auto result    = FB::plan(rectangle(), p);
    ready(result);
    REQUIRE(result.full_candidate);
    CHECK_THAT(result.local_profile.flow_ratio, WithinAbs(p.flow_ratio, 0.));
    CHECK_THAT(result.local_profile.print_flow_ratio, WithinAbs(p.print_flow_ratio, 0.));
    CHECK_THAT(result.local_profile.surface_flow_ratio, WithinAbs(p.surface_flow_ratio, 0.));
    CHECK_THAT(result.section.supplied_area, WithinAbs(result.section.area_before_flow * .97 * .95 * 1.03, 1e-14));
    CHECK_THAT(result.speed, WithinAbs(p.volumetric_limit / result.section.supplied_area, 1e-12));
    CHECK(result.speed <= p.speed);
    full(audit({sweep(result, 0., .2)}, 12., 6.1, .2), 14.64);
}

TEST_CASE("Infeasible width intervals and short turning regions are rejected with a reason", "[FormorixBoundary]")
{
    auto p               = profile();
    p.local_width_limits = PS::WidthLimits{.79, .80};
    const auto interval  = FB::plan(rectangle(), p);
    CHECK(interval.status == FB::Status::Unsupported);
    CHECK_FALSE(interval.reason.empty());
    CHECK(interval.path.empty());
    CHECK_FALSE(interval.full_candidate);
    const auto narrow = FB::plan(rectangle(.5, 3.), profile(false));
    CHECK(narrow.status == FB::Status::Unsupported);
    CHECK(narrow.path.empty());
    const auto strip = FB::plan(rectangle(12., .1), profile(false));
    CHECK(strip.status == FB::Status::Unsupported);
}

TEST_CASE("Holes and sloping boundaries cannot receive a rectangular fill candidate", "[FormorixBoundary]")
{
    FB::Domain hole;
    hole.vertices = {{0., 0.}, {4., 0.}, {4., 4.}, {0., 4.}, {1., 1.}, {3., 1.}, {3., 3.}, {1., 3.}};
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        hole.triangles.emplace_back(i, j, j + 4);
        hole.triangles.emplace_back(i, j + 4, i + 4);
    }
    const auto with_hole = FB::plan(hole, profile());
    CHECK(with_hole.status == FB::Status::Unsupported);
    CHECK_FALSE(with_hole.full_candidate);
    auto sloped            = rectangle(4., 4.);
    sloped.vertices[2].x() = 3.;
    const auto trapezoid   = FB::plan(sloped, profile());
    CHECK(trapezoid.status == FB::Status::Unsupported);
    CHECK_FALSE(trapezoid.full_candidate);
}

TEST_CASE("Raw boundary topology retains holes smaller than a planar integer grid", "[FormorixBoundary]")
{
    FB::Domain hole;
    hole.vertices = {{0., 0.}, {4., 0.}, {4., 4.}, {0., 4.}, {2., 2.}, {2. + 1e-9, 2.}, {2. + 1e-9, 2. + 1e-9}, {2., 2. + 1e-9}};
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        hole.triangles.emplace_back(i, j, j + 4);
        hole.triangles.emplace_back(i, j + 4, i + 4);
    }
    const auto result = FB::plan(hole, profile());
    CHECK(result.status == FB::Status::Unsupported);
    CHECK(result.path.empty());
}

TEST_CASE("Boundary layout rejects disconnected or invalid projections and honors its resource limits", "[FormorixBoundary]")
{
    auto domain       = rectangle();
    const auto second = rectangle();
    for (const auto& p : second.vertices)
        domain.vertices.push_back(p + Vec2d(20., 0.));
    for (const auto& t : second.triangles)
        domain.triangles.push_back(t + Vec3i32::Constant(4));
    CHECK(FB::plan(domain, profile()).status == FB::Status::Unsupported);
    domain = rectangle();
    domain.triangles.push_back(domain.triangles.front());
    CHECK(FB::plan(domain, profile()).status == FB::Status::Unsupported);
    domain                      = rectangle();
    domain.vertices.front().x() = std::numeric_limits<double>::quiet_NaN();
    CHECK(FB::plan(domain, profile()).status == FB::Status::InvalidInput);
    FB::Options options;
    options.max_rows = 1;
    CHECK(FB::plan(rectangle(), profile(), options).status == FB::Status::ResourceLimit);
    CHECK(FB::plan(rectangle(), profile(), {}, [] { return true; }).status == FB::Status::Canceled);
}
