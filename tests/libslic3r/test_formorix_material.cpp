#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixMaterial.hpp"
#include "libslic3r/FormorixGeometry.hpp"
#include "libslic3r/Point.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
namespace FM = FormorixMaterial;
namespace FG = FormorixGeometry;
using Catch::Matchers::WithinAbs;
namespace {
FG::Mesh box(const Vec3d& low, const Vec3d& high)
{
    FG::Mesh m;
    m.vertices = {{low.x(), low.y(), low.z()},    {high.x(), low.y(), low.z()}, {high.x(), high.y(), low.z()},
                  {low.x(), high.y(), low.z()},   {low.x(), low.y(), high.z()}, {high.x(), low.y(), high.z()},
                  {high.x(), high.y(), high.z()}, {low.x(), high.y(), high.z()}};
    m.faces    = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                  {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
    return m;
}
FM::Target target(int region = 1, const std::string& material = "PETG")
{
    FM::Target t;
    t.region                 = region;
    t.material               = material;
    t.mesh                   = box({0., 0., 0.}, {2., 1., 1.});
    t.outer_surface.vertices = {{0., 0., 1.}, {2., 0., 1.}, {2., 1., 1.}, {0., 1., 1.}};
    t.outer_surface.faces    = {{0, 1, 2}, {0, 2, 3}};
    return t;
}
FM::SweepRun run(double from = 0., double to = 2., int region = 1, const std::string& material = "PETG")
{
    FM::SweepRun r;
    r.region           = region;
    r.material         = material;
    r.stations         = {{{from, .5, .5}, Vec3d::UnitZ(), 1., 1.}, {{to, .5, .5}, Vec3d::UnitZ(), 1., 1.}};
    r.supplied_volumes = {to - from};
    r.outer_layer      = true;
    return r;
}
void require_complete(const FM::Result& result)
{
    INFO(result.error);
    REQUIRE(result.ok());
    REQUIRE(result.status == FM::Status::Complete);
    CHECK_THAT(result.input_rounding_volume_bound, WithinAbs(0., 0.));
    for (const auto& region : result.regions)
        CHECK_THAT(region.input_rounding_volume_bound, WithinAbs(0., 0.));
}
} // namespace

TEST_CASE("An exact finite material prism fills its target and separately bounds its outer sheet", "[FormorixMaterial]")
{
    const auto result = FM::audit({target()}, {}, {run()});
    require_complete(result);
    REQUIRE(result.regions.size() == 1);
    CHECK(result.regions[0].coverage == FM::Coverage::Full);
    CHECK_THAT(result.cell_volume, WithinAbs(2., 1e-12));
    CHECK_THAT(result.union_volume, WithinAbs(2., 1e-12));
    CHECK_THAT(result.supplied_volume, WithinAbs(2., 1e-12));
    CHECK_THAT(result.supply_geometry_residual, WithinAbs(0., 1e-12));
    CHECK_THAT(result.duplicate_volume, WithinAbs(0., 1e-12));
    CHECK_THAT(result.missing_volume, WithinAbs(0., 1e-12));
    CHECK_THAT(result.outside_volume, WithinAbs(0., 1e-12));
    CHECK(result.boundary_distance.bounded);
    CHECK(result.boundary_distance.upper_bound < 1e-10);
    CHECK(result.nominal_outer_distance.bounded);
    CHECK(result.nominal_outer_distance.upper_bound <= .001);
    CHECK_THAT(result.nominal_outer_distance.maximum, WithinAbs(0., 1e-12));
    CHECK(FG::valid_solid(result.deposited));
}

TEST_CASE("Mitered material turns preserve finite volume despite redundant collinear stations", "[FormorixMaterial]")
{
    auto t          = target();
    t.mesh          = box({0., 0., 0.}, {4., 2., 1.});
    t.outer_surface = {};
    auto r          = run();
    r.stations.clear();
    r.supplied_volumes.clear();
    for (const Vec3d& p : std::vector<Vec3d>{{0., .5, .5},
                                             {1., .5, .5},
                                             {2., .5, .5},
                                             {3., .5, .5},
                                             {3.5, .5, .5},
                                             {3.5, 1., .5},
                                             {3.5, 1.5, .5},
                                             {3., 1.5, .5},
                                             {2., 1.5, .5},
                                             {1., 1.5, .5},
                                             {0., 1.5, .5}}) {
        if (!r.stations.empty())
            r.supplied_volumes.push_back((p - r.stations.back().center).norm());
        r.stations.push_back({p, Vec3d::UnitZ(), 1., 1.});
    }
    const auto result = FM::audit({t}, {}, {r});
    require_complete(result);
    CHECK(result.cells == 3);
    CHECK_THAT(result.union_volume, WithinAbs(8., 1e-12));
    CHECK_THAT(result.supplied_volume, WithinAbs(8., 1e-12));
    CHECK_THAT(result.duplicate_volume, WithinAbs(0., 1e-12));
    CHECK_THAT(result.missing_volume, WithinAbs(0., 1e-12));
    CHECK_THAT(result.outside_volume, WithinAbs(0., 1e-12));
    CHECK(result.regions[0].coverage == FM::Coverage::Full);
}

TEST_CASE("A thin internal material gap survives exact union and cannot receive a zero boundary error", "[FormorixMaterial]")
{
    const auto result = FM::audit({target()}, {}, {run(0., 1. - 1e-10), run(1. + 1e-10, 2.)});
    require_complete(result);
    CHECK(result.regions[0].coverage == FM::Coverage::Partial);
    CHECK(result.missing_volume > 1e-10);
    CHECK(result.missing_volume < 3e-10);
    CHECK(result.boundary_distance.lower_bound > .3);
    CHECK(result.boundary_distance.upper_bound > .49);
    CHECK_THAT(result.construction_rounding_bound, WithinAbs(0., 1e-20));
}

TEST_CASE("Duplicate material is counted spatially without hiding it in a target clip", "[FormorixMaterial]")
{
    const auto result = FM::audit({target()}, {}, {run(), run()});
    require_complete(result);
    CHECK_THAT(result.union_volume, WithinAbs(2., 1e-12));
    CHECK_THAT(result.cell_volume, WithinAbs(4., 1e-12));
    CHECK_THAT(result.duplicate_volume, WithinAbs(2., 1e-12));
    CHECK_THAT(result.supplied_volume, WithinAbs(4., 1e-12));
    CHECK(result.regions[0].coverage == FM::Coverage::Full);
}

TEST_CASE("Different materials keep regional intrusion and cross-material overlap separate", "[FormorixMaterial]")
{
    auto second          = target(2, "PVA");
    second.mesh          = box({2., 0., 0.}, {4., 1., 1.});
    second.outer_surface = {};
    const auto result    = FM::audit({target(), second}, {}, {run(0., 2.5), run(1.5, 4., 2, "PVA")});
    require_complete(result);
    REQUIRE(result.regions.size() == 2);
    REQUIRE(result.material_overlaps.size() == 1);
    CHECK_THAT(result.material_overlaps[0].volume, WithinAbs(1., 1e-12));
    CHECK_THAT(result.regions[0].outside_volume, WithinAbs(.5, 1e-12));
    CHECK_THAT(result.regions[1].outside_volume, WithinAbs(.5, 1e-12));
    CHECK_THAT(result.outside_volume, WithinAbs(0., 1e-12));
    CHECK_THAT(result.duplicate_volume, WithinAbs(1., 1e-12));
}

TEST_CASE("Material below its allocated skin remains visible as core overlap and missing skin", "[FormorixMaterial]")
{
    auto r = run();
    for (auto& s : r.stations)
        s.center.z() = 0.;
    const auto result = FM::audit({target()}, box({0., 0., -1.}, {2., 1., 0.}), {r});
    require_complete(result);
    CHECK_THAT(result.core_overlap_volume, WithinAbs(1., 1e-12));
    CHECK_THAT(result.regions[0].core_overlap_volume, WithinAbs(1., 1e-12));
    CHECK_THAT(result.missing_volume, WithinAbs(1., 1e-12));
    CHECK_THAT(result.outside_volume, WithinAbs(1., 1e-12));
}

TEST_CASE("An empty material plan reports all missing volume and undefined distance statistics", "[FormorixMaterial]")
{
    const auto result = FM::audit({target()}, {}, {});
    require_complete(result);
    CHECK(result.regions[0].coverage == FM::Coverage::Empty);
    CHECK_THAT(result.missing_volume, WithinAbs(2., 1e-12));
    CHECK_FALSE(result.boundary_distance.bounded);
    CHECK(std::isinf(result.boundary_distance.maximum));
    CHECK(std::isinf(result.boundary_distance.mean));
    CHECK(std::isinf(result.boundary_distance.rms));
    CHECK_FALSE(FM::audit({}, {}, {run()}).ok());
}

TEST_CASE("A material target can retain an internal cavity instead of treating only its outer hull", "[FormorixMaterial]")
{
    auto t            = target();
    t.mesh            = box({0., 0., 0.}, {3., 3., 3.});
    const auto cavity = box({1., 1., 1.}, {2., 2., 2.});
    const int base    = int(t.mesh.vertices.size());
    t.mesh.vertices.insert(t.mesh.vertices.end(), cavity.vertices.begin(), cavity.vertices.end());
    for (const auto& f : cavity.faces)
        t.mesh.faces.emplace_back(base + f[2], base + f[1], base + f[0]);
    t.outer_surface = {};
    auto r          = run(0., 3.);
    for (auto& s : r.stations) {
        s.center.y() = s.center.z() = 1.5;
        s.width = s.height = 3.;
    }
    r.supplied_volumes[0] = 27.;
    const auto result     = FM::audit({t}, {}, {r});
    require_complete(result);
    CHECK_THAT(result.regions[0].target_volume, WithinAbs(26., 1e-12));
    CHECK_THAT(result.outside_volume, WithinAbs(1., 1e-12));
    CHECK(result.boundary_distance.lower_bound > .99);
}

TEST_CASE("Tilted nominal cells retain their real geometric volume instead of adjusting the supplied flow", "[FormorixMaterial]")
{
    auto r = run();
    for (auto& s : r.stations)
        s.axis = Vec3d(.6, 0., .8);
    auto t          = target();
    t.mesh          = box({-1., -1., -1.}, {3., 2., 2.});
    t.outer_surface = {};
    FM::Options options;
    options.max_distance_queries = 1;
    const auto result            = FM::audit({t}, {}, {r}, options);
    require_complete(result);
    CHECK_THAT(result.supplied_volume, WithinAbs(2., 1e-12));
    CHECK_THAT(result.cell_volume, WithinAbs(1.6, 1e-12));
    CHECK_THAT(result.supply_geometry_residual, WithinAbs(.4, 1e-12));
    CHECK_THAT(result.regions[0].supply_geometry_residual, WithinAbs(.4, 1e-12));
}

TEST_CASE("Unrepresentable tilted Boolean output is rejected without a hidden topology repair", "[FormorixMaterial]")
{
    auto r = run();
    for (auto& s : r.stations)
        s.axis = Vec3d(.6, 0., .8);
    FM::Options options;
    options.max_distance_queries = 1;
    const auto result            = FM::audit({target()}, {}, {r}, options);
    CHECK_FALSE(result.ok());
    CHECK(result.error.find("output collapses topology") != std::string::npos);
    CHECK(result.regions.empty());
    CHECK(std::isinf(result.boundary_distance.maximum));
    CHECK_FALSE(result.diagnostic_cells.faces.empty());
}

TEST_CASE("Curved radial cells preserve their spatial sector volume and expose the independent supply residual", "[FormorixMaterial]")
{
    FM::SweepRun r;
    r.region            = 1;
    r.material          = "PETG";
    const double radius = 2., height = .2;
    for (const Vec3d& n : std::vector<Vec3d>{{1., 0., 0.}, {.6, 0., .8}, {-.28, 0., .96}}) {
        const Vec3d p = radius * n;
        if (!r.stations.empty())
            r.supplied_volumes.push_back(height * (p - r.stations.back().center).norm());
        r.stations.push_back({p, n, 1., height});
    }
    auto t          = target();
    t.mesh          = box({-2., -1., -1.}, {3., 1., 3.});
    t.outer_surface = {};
    FM::Options options;
    options.max_distance_queries = 1;
    const auto result            = FM::audit({t}, {}, {r}, options);
    require_complete(result);
    const double expected_volume = 2. * radius * height * .8;
    CHECK_THAT(result.cell_volume, WithinAbs(expected_volume, 1e-12));
    CHECK_THAT(result.union_volume, WithinAbs(expected_volume, 1e-12));
    CHECK_THAT(result.duplicate_volume, WithinAbs(0., 1e-12));
    CHECK(result.supply_geometry_residual > .07);
    CHECK_FALSE(result.boundary_distance.bounded);
    CHECK(result.distance_queries == 1);
}

TEST_CASE("Failed or canceled material audits preserve only diagnostic cells and no accepted evidence", "[FormorixMaterial]")
{
    auto r = run();
    r.stations.push_back(r.stations.front());
    r.supplied_volumes.push_back(2.);
    const auto folded = FM::audit({target()}, {}, {r});
    CHECK_FALSE(folded.ok());
    CHECK(folded.regions.empty());
    CHECK_FALSE(folded.boundary_distance.bounded);
    const auto canceled = FM::audit({target()}, {}, {run()}, {}, [] { return true; });
    CHECK(canceled.status == FM::Status::Canceled);
    CHECK(canceled.regions.empty());
    FM::Options options;
    options.max_boolean_faces = 12;
    auto large                = target();
    large.mesh.vertices.push_back(Vec3d::Zero());
    large.mesh.faces.push_back({0, 1, 2});
    const auto limited = FM::audit({large}, {}, {run()}, options);
    CHECK(limited.status == FM::Status::ResourceLimit);
    CHECK(limited.diagnostic_cells.faces.empty());
    CHECK(limited.regions.empty());
    options                        = {};
    options.max_boolean_operations = 1;
    const auto work_limited        = FM::audit({target()}, {}, {run(), run()}, options);
    CHECK(work_limited.status == FM::Status::ResourceLimit);
    CHECK_FALSE(work_limited.diagnostic_cells.faces.empty());
}

TEST_CASE("An exhausted distance budget never promotes samples to a complete surface bound", "[FormorixMaterial]")
{
    FM::Options options;
    options.max_distance_leaves = 1;
    const auto result           = FM::audit({target()}, {}, {run(0., 1.)}, options);
    require_complete(result);
    CHECK_FALSE(result.boundary_distance.bounded);
    CHECK(std::isinf(result.boundary_distance.upper_bound));
    CHECK(std::isinf(result.boundary_distance.mean));

    options                      = {};
    options.max_distance_queries = 1;
    const auto one_query         = FM::audit({target()}, {}, {run(0., 1.)}, options);
    require_complete(one_query);
    CHECK(one_query.distance_queries == 1);
    CHECK(one_query.boundary_distance.samples == 1);
    CHECK_FALSE(one_query.boundary_distance.bounded);
    CHECK(std::isinf(one_query.boundary_distance.maximum));
    CHECK(std::isinf(one_query.boundary_distance.upper_bound));
    CHECK(std::isinf(one_query.boundary_distance.mean));
    CHECK(std::isinf(one_query.nominal_outer_distance.maximum));
}

TEST_CASE("Large segment supply ledgers avoid a recursive lazy arithmetic chain", "[FormorixMaterial]")
{
    auto r = run();
    r.stations.clear();
    r.supplied_volumes.clear();
    constexpr std::size_t segments = 32768;
    for (std::size_t i = 0; i <= segments; ++i) {
        r.stations.push_back({Vec3d(2. * double(i) / segments, .5, .5), Vec3d::UnitZ(), 1., 1.});
        if (i)
            r.supplied_volumes.push_back(2. / segments);
    }
    auto t            = target();
    t.outer_surface   = {};
    const auto result = FM::audit({t}, {}, {r});
    require_complete(result);
    CHECK(result.cells == 1);
    CHECK_THAT(result.supplied_volume, WithinAbs(2., 1e-12));
    CHECK_THAT(result.supply_geometry_residual, WithinAbs(0., 1e-12));
}

TEST_CASE("Explicit adjacent layer endpoints retain their shared interface without audit snapping", "[FormorixMaterial]")
{
    auto t          = target();
    t.mesh          = box({0., 0., 0.}, {2., 1., .4});
    t.outer_surface = {};
    std::vector<FM::SweepRun> layers;
    const double boundaries[] = {0., .2, .4};
    for (int layer = 0; layer < 2; ++layer) {
        auto r             = run();
        r.layer            = layer;
        r.supplied_volumes = {.4};
        for (auto& station : r.stations) {
            station.height           = .2;
            station.center.z()       = (boundaries[layer] + boundaries[layer + 1]) * .5;
            station.height_endpoints = std::array<Vec3d, 2>{
                {Vec3d(station.center.x(), .5, boundaries[layer]), Vec3d(station.center.x(), .5, boundaries[layer + 1])}};
        }
        layers.push_back(std::move(r));
    }
    const auto result = FM::audit({t}, {}, layers);
    require_complete(result);
    CHECK(result.regions[0].coverage == FM::Coverage::Full);
    CHECK_THAT(result.duplicate_volume, WithinAbs(0., 1e-15));
    CHECK_THAT(result.missing_volume, WithinAbs(0., 1e-15));
    CHECK_THAT(result.union_volume, WithinAbs(.8, 1e-15));
    CHECK_THAT(result.construction_rounding_bound, WithinAbs(0., 1e-20));
    (*layers[0].stations[0].height_endpoints)[0].z() += .01;
    CHECK_FALSE(FM::audit({t}, {}, layers).ok());
}

TEST_CASE("Invalid height endpoints are rejected before a collinear station can be removed", "[FormorixMaterial]")
{
    auto r = run();
    r.stations.insert(r.stations.begin() + 1, {{1., .5, .5}, Vec3d::UnitZ(), 1., 1.});
    r.supplied_volumes = {1., 1.};
    for (auto& s : r.stations)
        s.height_endpoints = std::array<Vec3d, 2>{{Vec3d(s.center.x(), .5, 0.), Vec3d(s.center.x(), .5, 1.)}};
    for (auto& endpoint : *r.stations[1].height_endpoints)
        endpoint.x() += .25;
    const auto result = FM::audit({target()}, {}, {r});
    CHECK(result.status == FM::Status::InvalidInput);
    CHECK(result.error.find("endpoints disagree") != std::string::npos);
    CHECK(result.regions.empty());
}

TEST_CASE("Ordered raster runs unite into a closed multilayer material body before spatial grouping", "[FormorixMaterial]")
{
    auto t          = target();
    t.mesh          = box({0., 0., 0.}, {3., 3., .4});
    t.outer_surface = {};
    std::vector<FM::SweepRun> runs;
    const double heights[] = {0., .2, .4};
    for (int layer = 0; layer < 2; ++layer) {
        FM::SweepRun r;
        r.region            = 1;
        r.material          = "PETG";
        r.layer             = layer;
        const double center = (heights[layer] + heights[layer + 1]) * .5;
        const std::vector<Vec3d> path{{0., .375, center},    {2.625, .375, center},  {2.625, 1.125, center}, {.375, 1.125, center},
                                      {.375, 1.875, center}, {2.625, 1.875, center}, {2.625, 2.625, center}, {0., 2.625, center}};
        for (const auto& p : path) {
            if (!r.stations.empty())
                r.supplied_volumes.push_back(.75 * .2 * (p - r.stations.back().center).norm());
            FM::Station s{p, Vec3d::UnitZ(), .75, .2};
            s.height_endpoints = std::array<Vec3d, 2>{{Vec3d(p.x(), p.y(), heights[layer]), Vec3d(p.x(), p.y(), heights[layer + 1])}};
            r.stations.push_back(std::move(s));
        }
        runs.push_back(std::move(r));
    }
    const auto result = FM::audit({t}, {}, runs);
    require_complete(result);
    CHECK(result.cells == 14);
    CHECK(result.regions[0].coverage == FM::Coverage::Full);
    CHECK_THAT(result.union_volume, WithinAbs(3.6, 1e-12));
    CHECK_THAT(result.duplicate_volume, WithinAbs(0., 1e-14));
    CHECK_THAT(result.missing_volume, WithinAbs(0., 1e-14));
    CHECK_THAT(result.outside_volume, WithinAbs(0., 1e-14));
}

TEST_CASE("Repeated audits initialize every regional ledger including empty material input", "[FormorixMaterial]")
{
    for (int i = 0; i < 4; ++i) {
        auto shifted = run();
        for (auto& station : shifted.stations)
            station.center.z() -= .5;
        const auto first = FM::audit({target()}, {}, {shifted});
        require_complete(first);
        const auto full = FM::audit({target()}, {}, {run()});
        require_complete(full);
        CHECK_THAT(full.supplied_volume, WithinAbs(2., 0.));
        CHECK_THAT(full.cell_volume, WithinAbs(2., 0.));
        CHECK_THAT(full.supply_geometry_residual, WithinAbs(0., 0.));
        const auto empty = FM::audit({target()}, {}, {});
        require_complete(empty);
        CHECK_THAT(empty.supplied_volume, WithinAbs(0., 0.));
        CHECK_THAT(empty.cell_volume, WithinAbs(0., 0.));
        CHECK_THAT(empty.regions[0].supply_geometry_residual, WithinAbs(0., 0.));
    }
}

TEST_CASE("A nonmanifold intermediate union returns diagnostics without traversing failed CGAL output", "[FormorixMaterial]")
{
    auto t          = target();
    t.mesh          = box({0., 0., 0.}, {4., 2., 1.});
    t.outer_surface = {};
    auto first = run(), second = run(2., 4.);
    for (auto& station : second.stations)
        station.center.y() = 1.5;
    const auto result = FM::audit({t}, {}, {first, second});
    CHECK(result.status == FM::Status::BooleanFailure);
    CHECK(result.error.find("region 1") != std::string::npos);
    CHECK(result.regions.empty());
    CHECK_FALSE(result.diagnostic_cells.faces.empty());
    CHECK(std::isinf(result.boundary_distance.upper_bound));
}

TEST_CASE("An isolated input vertex is rejected before invoking volume orientation predicates", "[FormorixMaterial]")
{
    auto t = target();
    t.mesh.vertices.emplace_back(50., 50., 50.);
    const auto result = FM::audit({t}, {}, {run()});
    CHECK_FALSE(result.ok());
    CHECK(result.regions.empty());
    CHECK(std::isinf(result.boundary_distance.maximum));
}

TEST_CASE("Invalid lateral endpoints are rejected before a redundant station can be removed", "[FormorixMaterial]")
{
    auto r = run();
    r.stations.insert(r.stations.begin() + 1, {{1., .5, .5}, Vec3d::UnitZ(), 1., 1.});
    r.supplied_volumes = {1., 1.};
    for (auto& s : r.stations)
        s.lateral_endpoints = std::array<Vec3d, 2>{{{s.center.x(), 0., .5}, {s.center.x(), 1., .5}}};
    for (auto& endpoint : *r.stations[1].lateral_endpoints)
        endpoint.y() += .25;
    const auto result = FM::audit({target()}, {}, {r});
    CHECK(result.status == FM::Status::InvalidInput);
    CHECK(result.error.find("lateral endpoints disagree") != std::string::npos);
    CHECK(result.regions.empty());
}
