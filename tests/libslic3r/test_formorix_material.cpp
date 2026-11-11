#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixMaterial.hpp"
#include "libslic3r/FormorixGeometry.hpp"
#include "libslic3r/FormorixDeposition.hpp"
#include "libslic3r/FormorixProcess.hpp"
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
namespace PS = FormorixProcess;
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
FG::Mesh annular_prism(const std::vector<Vec2d>& outer, const std::vector<Vec2d>& inner, double lower, double upper)
{
    FG::Mesh mesh;
    const int n = int(outer.size());
    for (double z : {lower, upper})
        for (const auto* contour : {&outer, &inner})
            for (const auto& p : *contour)
                mesh.vertices.emplace_back(p.x(), p.y(), z);
    auto quad = [&](int a, int b, int c, int d) {
        mesh.faces.emplace_back(a, b, c);
        mesh.faces.emplace_back(a, c, d);
    };
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        quad(2 * n + i, 2 * n + j, 3 * n + j, 3 * n + i);
        quad(i, n + i, n + j, j);
        quad(i, j, 2 * n + j, 2 * n + i);
        quad(n + i, 3 * n + i, 3 * n + j, n + j);
    }
    return mesh;
}
FM::Target square_ring_target()
{
    auto t          = target();
    t.mesh          = annular_prism({{-.5, -.5}, {4.5, -.5}, {4.5, 4.5}, {-.5, 4.5}}, {{.5, .5}, {3.5, .5}, {3.5, 3.5}, {.5, 3.5}}, 0., 1.);
    t.outer_surface = {};
    return t;
}
void ring_supply(FM::SweepRun& r)
{
    r.supplied_volumes.clear();
    for (std::size_t i = 0; i < r.stations.size(); ++i) {
        const auto& a = r.stations[i];
        const auto& b = r.stations[(i + 1) % r.stations.size()];
        r.supplied_volumes.push_back((b.center - a.center).norm() * a.width * a.height);
    }
}
FM::SweepRun square_ring()
{
    auto r   = run();
    r.closed = true;
    r.stations.clear();
    for (const Vec3d& p : std::vector<Vec3d>{{0., 0., .5}, {4., 0., .5}, {4., 4., .5}, {0., 4., .5}})
        r.stations.push_back({p, Vec3d::UnitZ(), 1., 1.});
    ring_supply(r);
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

struct ThreeMaterialRings
{
    std::array<PS::Profile, 3> profiles;
    std::array<PS::RibbonSection, 3> sections;
    std::array<double, 3> inner, outer, radius, volume;
    std::vector<FM::Target> targets;
    std::vector<FM::SweepRun> runs;
};

ThreeMaterialRings three_material_rings()
{
    ThreeMaterialRings fixture;
    const std::array<double, 3> nozzles{.8, .4, .6}, widths{.75, .375, .5625}, heights{.25, .125, .1875};
    const std::array<std::string, 3> materials{"PETG", "PVA", "PETG-CF"};
    double inner = 2.;
    for (std::size_t i = 0; i < 3; ++i) {
        auto& p           = fixture.profiles[i];
        p.material_id     = materials[i];
        p.nozzle_profile  = "head-" + std::to_string(i);
        p.nozzle_diameter = nozzles[i];
        p.layer_height    = heights[i];
        // Invert the documented stadium-to-ribbon model, without changing Flow.
        // Binary compacted widths make the shared rails and 45-degree miters exact.
        p.line_width = widths[i] + heights[i] * (1. - std::acos(-1.) / 4.);
        p.flow_ratio = p.print_flow_ratio = p.surface_flow_ratio = 1.;
        fixture.sections[i]                                      = PS::ribbon_section(p);
        REQUIRE_THAT(fixture.sections[i].compacted_width, WithinAbs(widths[i], 0.));
        const double outer = inner + widths[i], radius = (inner + outer) / 2.;
        fixture.inner[i]  = inner;
        fixture.outer[i]  = outer;
        fixture.radius[i] = radius;
        fixture.volume[i] = 4. * (outer * outer - inner * inner) * heights[i];
        auto t            = target(int(i + 1), materials[i]);
        t.mesh = annular_prism({{-outer, -outer}, {outer, -outer}, {outer, outer}, {-outer, outer}},
                               {{-inner, -inner}, {inner, -inner}, {inner, inner}, {-inner, inner}}, -heights[i] / 2., heights[i] / 2.);
        t.outer_surface = {};
        fixture.targets.push_back(std::move(t));
        auto r        = square_ring();
        r.head        = int(i);
        r.region      = int(i + 1);
        r.material    = materials[i];
        r.outer_layer = false;
        r.stations.clear();
        Vec3d axis = Vec3d::UnitZ();
        if (i == 1)
            axis = -axis;
        for (const Vec3d& v : std::vector<Vec3d>{{-radius, -radius, 0.}, {radius, -radius, 0.}, {radius, radius, 0.}, {-radius, radius, 0.}})
            r.stations.push_back({v, axis, widths[i], heights[i]});
        ring_supply(r);
        fixture.runs.push_back(std::move(r));
        inner = outer;
    }
    return fixture;
}
} // namespace

TEST_CASE("Closed material rings have one cyclic miter at every station and no artificial seam", "[FormorixMaterial]")
{
    const auto t = square_ring_target();
    REQUIRE(FG::valid_solid(t.mesh));
    for (bool reverse : {false, true})
        for (int start : {0, 1, 5}) {
            auto r             = square_ring();
            const auto corners = r.stations;
            r.stations.clear();
            for (std::size_t i = 0; i < corners.size(); ++i) {
                r.stations.push_back(corners[i]);
                auto mid   = corners[i];
                mid.center = .5 * (corners[i].center + corners[(i + 1) % corners.size()].center);
                r.stations.push_back(mid);
            }
            if (reverse)
                std::reverse(r.stations.begin(), r.stations.end());
            std::rotate(r.stations.begin(), r.stations.begin() + start, r.stations.end());
            ring_supply(r);
            const auto result = FM::audit({t}, {}, {r});
            require_complete(result);
            CHECK(result.cells == 4);
            CHECK(result.regions.front().coverage == FM::Coverage::Full);
            CHECK_THAT(result.union_volume, WithinAbs(16., 1e-12));
            CHECK_THAT(result.supplied_volume, WithinAbs(16., 1e-12));
            CHECK_THAT(result.missing_volume, WithinAbs(0., 1e-12));
            CHECK_THAT(result.outside_volume, WithinAbs(0., 1e-12));
            CHECK_THAT(result.duplicate_volume, WithinAbs(0., 1e-12));
            CHECK(result.boundary_distance.bounded);
        }
}

TEST_CASE("A closed polygonal cylinder ring retains radial nozzle axes and its independent supply ledger", "[FormorixMaterial]")
{
    const std::vector<Vec2d> directions{{1., 0.}, {0., 1.}, {-1., 0.}, {0., -1.}};
    std::vector<Vec2d> outer, inner;
    auto r = square_ring();
    r.stations.clear();
    for (const auto& d : directions) {
        outer.push_back(2.125 * d);
        inner.push_back(1.875 * d);
        const Vec3d axis(d.x(), d.y(), 0.);
        r.stations.push_back({2. * axis, axis, .5, .25});
    }
    ring_supply(r);
    auto t          = target();
    t.mesh          = annular_prism(outer, inner, -.25, .25);
    t.outer_surface = {};
    REQUIRE(FG::valid_solid(t.mesh));
    const auto result = FM::audit({t}, {}, {r});
    require_complete(result);
    CHECK(result.cells == 4);
    CHECK(result.regions.front().coverage == FM::Coverage::Full);
    CHECK_THAT(result.union_volume, WithinAbs(1., 1e-12));
    CHECK_THAT(result.supplied_volume, WithinAbs(std::sqrt(2.), 1e-12));
    CHECK_THAT(result.supply_geometry_residual, WithinAbs(std::sqrt(2.) - 1., 1e-12));
    CHECK_THAT(result.missing_volume, WithinAbs(0., 1e-12));
    CHECK_THAT(result.outside_volume, WithinAbs(0., 1e-12));
    CHECK_THAT(result.duplicate_volume, WithinAbs(0., 1e-12));
}

TEST_CASE("Open material paths do not acquire an implicit closing extrusion", "[FormorixMaterial]")
{
    auto r   = square_ring();
    r.closed = false;
    r.supplied_volumes.pop_back();
    const auto result = FM::audit({square_ring_target()}, {}, {r});
    require_complete(result);
    CHECK(result.cells == 3);
    CHECK(result.regions.front().coverage == FM::Coverage::Partial);
    CHECK_THAT(result.union_volume, WithinAbs(12., 1e-12));
    CHECK_THAT(result.supplied_volume, WithinAbs(12., 1e-12));
    CHECK_THAT(result.missing_volume, WithinAbs(4., 1e-12));
}

TEST_CASE("Closed material inputs validate the closing supply and original endpoint definitions", "[FormorixMaterial]")
{
    const auto t = square_ring_target();
    auto r       = square_ring();
    r.supplied_volumes.pop_back();
    CHECK(FM::audit({t}, {}, {r}).status == FM::Status::InvalidInput);
    r = square_ring();
    r.stations.push_back(r.stations.front());
    r.supplied_volumes.push_back(1.);
    CHECK(FM::audit({t}, {}, {r}).status == FM::Status::InvalidInput);
    r                                    = square_ring();
    r.stations.front().lateral_endpoints = std::array<Vec3d, 2>{{Vec3d(-.5, -.5, .5), Vec3d(.5, .5, .5)}};
    require_complete(FM::audit({t}, {}, {r}));
    (*r.stations.front().lateral_endpoints)[0].x() += .01;
    CHECK(FM::audit({t}, {}, {r}).status == FM::Status::InvalidInput);
    r                         = square_ring();
    r.supplied_volumes.back() = 0.;
    CHECK(FM::audit({t}, {}, {r}).status == FM::Status::InvalidInput);
    r.stations.resize(2);
    r.supplied_volumes.assign(2, 1.);
    CHECK(FM::audit({t}, {}, {r}).status == FM::Status::InvalidInput);
}

TEST_CASE("Closed sweep budgets include the closing cell and cancellation discards evidence", "[FormorixMaterial]")
{
    const auto t = square_ring_target();
    const auto r = square_ring();
    FM::Options options;
    options.max_sweep_faces = 36; // target has 32 faces; four cells require 48 faces.
    CHECK(FM::audit({t}, {}, {r}, options).status == FM::Status::ResourceLimit);
    options              = {};
    options.max_stations = 3;
    CHECK(FM::audit({t}, {}, {r}, options).status == FM::Status::ResourceLimit);
    std::size_t checks  = 0;
    const auto canceled = FM::audit({t}, {}, {r}, {}, [&] { return ++checks > 8; });
    CHECK(canceled.status == FM::Status::Canceled);
    CHECK(canceled.regions.empty());
    CHECK(canceled.deposited.faces.empty());
}

TEST_CASE("Material time budgets expire without waiting and retain no accepted evidence", "[FormorixMaterial]")
{
    FM::Options options;
    CHECK_THAT(options.max_elapsed_seconds, WithinAbs(60., 0.));
    options.max_elapsed_seconds = 0.;
    const auto result           = FM::audit({target()}, {}, {run()}, options);
    CHECK(result.status == FM::Status::ResourceLimit);
    CHECK(result.error.find("elapsed time budget") != std::string::npos);
    CHECK(std::isfinite(result.elapsed_seconds));
    CHECK(result.elapsed_seconds >= 0.);
    CHECK_FALSE(result.ok());
    CHECK(result.regions.empty());
    CHECK(result.deposited.faces.empty());
    CHECK(result.material_overlaps.empty());
    CHECK_FALSE(result.boundary_distance.bounded);
    CHECK_FALSE(result.nominal_outer_distance.bounded);
    CHECK(std::isinf(result.boundary_distance.upper_bound));
    CHECK(std::isinf(result.nominal_outer_distance.upper_bound));

    // Explicit user cancellation takes priority over the simultaneous deadline.
    const auto canceled = FM::audit({target()}, {}, {run()}, options, [] { return true; });
    CHECK(canceled.status == FM::Status::Canceled);
    CHECK(canceled.regions.empty());
    CHECK(canceled.deposited.faces.empty());
    CHECK(std::isfinite(canceled.elapsed_seconds));
    CHECK(canceled.elapsed_seconds >= 0.);
}

TEST_CASE("Material time limits require finite nonnegative seconds", "[FormorixMaterial]")
{
    for (double invalid : {-1., std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        CAPTURE(invalid);
        FM::Options options;
        options.max_elapsed_seconds = invalid;
        const auto result           = FM::audit({target()}, {}, {run()}, options);
        CHECK(result.status == FM::Status::InvalidInput);
        CHECK(result.regions.empty());
        CHECK_FALSE(result.boundary_distance.bounded);
        CHECK(std::isfinite(result.elapsed_seconds));
        CHECK(result.elapsed_seconds >= 0.);
    }
}

TEST_CASE("An exact finite material prism fills its target and separately bounds its outer sheet", "[FormorixMaterial]")
{
    const auto result = FM::audit({target()}, {}, {run()});
    require_complete(result);
    CHECK(std::isfinite(result.elapsed_seconds));
    CHECK(result.elapsed_seconds >= 0.);
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

TEST_CASE("Three material bodies share exact mitered interfaces with independent nozzle sections and axes",
          "[FormorixMaterial][MultiMaterialBoundary]")
{
    for (bool rotate : {false, true}) {
        CAPTURE(rotate);
        auto fixture = three_material_rings();
        // An exact cyclic coordinate permutation also tests vertical ring planes.
        // Opposing axes are nominal geometry inputs, not a machine reachability claim.
        if (rotate) {
            for (auto& t : fixture.targets)
                for (auto& v : t.mesh.vertices)
                    v = Vec3d(v.z(), v.x(), v.y());
            for (auto& r : fixture.runs)
                for (auto& s : r.stations) {
                    s.center = Vec3d(s.center.z(), s.center.x(), s.center.y());
                    s.axis   = Vec3d(s.axis.z(), s.axis.x(), s.axis.y());
                }
        }
        double expected_volume = 0.;
        for (std::size_t i = 0; i < 3; ++i) {
            REQUIRE(FG::valid_solid(fixture.targets[i].mesh));
            expected_volume += fixture.volume[i];
        }
        const auto result = FM::audit(fixture.targets, {}, fixture.runs);
        require_complete(result);
        REQUIRE(result.regions.size() == 3);
        CHECK(result.cells == 12);
        CHECK(result.material_overlaps.empty());
        CHECK_THAT(result.union_volume, WithinAbs(expected_volume, 1e-12));
        CHECK_THAT(result.supplied_volume, WithinAbs(expected_volume, 1e-12));
        CHECK_THAT(result.supply_geometry_residual, WithinAbs(0., 1e-12));
        CHECK_THAT(result.missing_volume, WithinAbs(0., 0.));
        CHECK_THAT(result.outside_volume, WithinAbs(0., 0.));
        CHECK_THAT(result.duplicate_volume, WithinAbs(0., 0.));
        CHECK_THAT(result.core_overlap_volume, WithinAbs(0., 0.));
        for (std::size_t i = 0; i < 3; ++i) {
            const auto& region = result.regions[i];
            CAPTURE(i, region.material);
            CHECK(region.region == int(i + 1));
            CHECK(region.material == fixture.profiles[i].material_id);
            CHECK(region.coverage == FM::Coverage::Full);
            CHECK_THAT(region.target_volume, WithinAbs(fixture.volume[i], 1e-12));
            CHECK_THAT(region.supplied_volume, WithinAbs(fixture.volume[i], 1e-12));
            CHECK_THAT(region.deposited_volume, WithinAbs(fixture.volume[i], 1e-12));
            CHECK_THAT(region.outside_volume, WithinAbs(0., 0.));
            CHECK_THAT(region.duplicate_volume, WithinAbs(0., 0.));
            CHECK_THAT(region.missing_volume, WithinAbs(0., 0.));
            CHECK_THAT(PS::effective_surface_flow(fixture.profiles[i]), WithinAbs(1., 0.));
            CHECK_THAT(fixture.runs[i].stations.front().width, WithinAbs(fixture.sections[i].compacted_width, 0.));
        }
    }
}

TEST_CASE("A local gap and a different material intrusion cannot cancel across three mitered bodies",
          "[FormorixMaterial][MultiMaterialBoundary]")
{
    // Test either defect alone and both together. Their analytical volumes are
    // deliberately close: the global supply balance alone cannot diagnose them.
    for (int defects : {1, 2, 3}) {
        CAPTURE(defects);
        const bool gap = (defects & 1) != 0, intrusion = (defects & 2) != 0;
        auto fixture            = three_material_rings();
        const double gap_length = .25, intrusion_length = .5;
        const double gap_volume            = gap ? gap_length * fixture.sections[2].supplied_area : 0.;
        const double extra_supply          = intrusion ? intrusion_length * fixture.sections[1].supplied_area : 0.;
        const double cross_material_volume = extra_supply / 2.;
        if (gap) {
            // Keep every corner miter; remove only a centered piece of the lower
            // straight edge of material C using two exact finite butt caps.
            auto& r            = fixture.runs[2];
            const auto corners = r.stations;
            auto start = corners[0], finish = corners[0];
            start.center.x()  = gap_length / 2.;
            finish.center.x() = -gap_length / 2.;
            r.closed          = false;
            r.stations        = {start, corners[1], corners[2], corners[3], corners[0], finish};
            r.supplied_volumes.clear();
            for (std::size_t i = 1; i < r.stations.size(); ++i)
                r.supplied_volumes.push_back((r.stations[i].center - r.stations[i - 1].center).norm() * fixture.sections[2].supplied_area);
        }
        if (intrusion) {
            // An actual extra B strand straddles the opposite B/C interface.
            // Half duplicates B; half overlaps C. Its complete unchanged supply
            // remains in the ledger, even though no material is globally outside.
            auto r     = fixture.runs[1];
            auto first = r.stations.front(), last = first;
            first.center       = Vec3d(-intrusion_length / 2., fixture.outer[1], 0.);
            last.center        = Vec3d(intrusion_length / 2., fixture.outer[1], 0.);
            r.closed           = false;
            r.stations         = {first, last};
            r.supplied_volumes = {extra_supply};
            fixture.runs.push_back(std::move(r));
        }
        FM::Options options;
        // Volume/Boolean identities are exact; this test does not request a
        // high-resolution boundary-distance certificate for deliberately bad plans.
        options.max_distance_queries = 1000;
        const auto result            = FM::audit(fixture.targets, {}, fixture.runs, options);
        require_complete(result);
        REQUIRE(result.regions.size() == 3);
        const double target_volume = fixture.volume[0] + fixture.volume[1] + fixture.volume[2];
        CHECK_THAT(result.missing_volume, WithinAbs(gap_volume, 1e-12));
        CHECK_THAT(result.duplicate_volume, WithinAbs(extra_supply, 1e-12));
        CHECK_THAT(result.union_volume, WithinAbs(target_volume - gap_volume, 1e-12));
        CHECK_THAT(result.supplied_volume, WithinAbs(target_volume - gap_volume + extra_supply, 1e-12));
        CHECK_THAT(result.supply_geometry_residual, WithinAbs(0., 1e-12));
        CHECK_THAT(result.outside_volume, WithinAbs(0., 0.));
        CHECK_THAT(result.core_overlap_volume, WithinAbs(0., 0.));
        CHECK(result.regions[0].coverage == FM::Coverage::Full);
        CHECK(result.regions[1].coverage == FM::Coverage::Full);
        const auto expected_coverage = gap ? FM::Coverage::Partial : FM::Coverage::Full;
        CHECK(result.regions[2].coverage == expected_coverage);
        CHECK_THAT(result.regions[0].outside_volume, WithinAbs(0., 0.));
        CHECK_THAT(result.regions[0].missing_volume, WithinAbs(0., 0.));
        CHECK_THAT(result.regions[1].outside_volume, WithinAbs(cross_material_volume, 1e-12));
        CHECK_THAT(result.regions[1].duplicate_volume, WithinAbs(extra_supply / 2., 1e-12));
        CHECK_THAT(result.regions[2].missing_volume, WithinAbs(gap_volume, 1e-12));
        CHECK_THAT(result.regions[2].outside_volume, WithinAbs(0., 0.));
        CHECK_THAT(result.regions[2].duplicate_volume, WithinAbs(0., 0.));
        if (gap) {
            REQUIRE_FALSE(result.missing.vertices.empty());
            for (const auto& v : result.missing.vertices) {
                CHECK(std::abs(v.x()) <= gap_length / 2.);
                CHECK(v.y() >= -fixture.outer[2]);
                CHECK(v.y() <= -fixture.inner[2]);
            }
        }
        if (intrusion) {
            REQUIRE(result.material_overlaps.size() == 1);
            const auto& overlap = result.material_overlaps.front();
            std::array<std::string, 2> names{overlap.first, overlap.second};
            std::sort(names.begin(), names.end());
            CHECK(names[0] == "PETG-CF");
            CHECK(names[1] == "PVA");
            CHECK_THAT(overlap.volume, WithinAbs(cross_material_volume, 1e-12));
            REQUIRE_FALSE(overlap.mesh.vertices.empty());
            for (const auto& v : overlap.mesh.vertices) {
                CHECK(std::abs(v.x()) <= intrusion_length / 2.);
                CHECK(v.y() >= fixture.outer[1]);
                CHECK(v.y() <= fixture.outer[1] + fixture.sections[1].compacted_width / 2.);
            }
        } else {
            CHECK(result.material_overlaps.empty());
        }
    }
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

TEST_CASE("Exact output retriangulation preserves the deficits and excess of a tilted material cell", "[FormorixMaterial]")
{
    auto r = run();
    for (auto& s : r.stations)
        s.axis = Vec3d(.6, 0., .8);
    FM::Options options;
    options.max_distance_queries = 1;
    const auto result            = FM::audit({target()}, {}, {r}, options);
    require_complete(result);
    REQUIRE(result.regions.size() == 1);
    CHECK(result.output_retriangulations > 0);
    // The tilted cross-section projects to 0.8 mm in Z, so the cell holds
    // 2 * 1 * 0.8 mm^3. Each finite end protrudes by a triangular wedge with
    // area 0.5 * 0.3 * 0.4 mm^2. Exact output retriangulation cannot remove
    // these wedges or fill the corresponding missing target material.
    const double cell_volume    = 2. * .8;
    const double outside_volume = 2. * .5 * .3 * .4;
    const double covered_volume = cell_volume - outside_volume;
    const double missing_volume = 2. - covered_volume;
    CHECK_THAT(result.supplied_volume, WithinAbs(2., 1e-12));
    CHECK_THAT(result.cell_volume, WithinAbs(cell_volume, 1e-12));
    CHECK_THAT(result.union_volume, WithinAbs(cell_volume, 1e-12));
    CHECK_THAT(result.outside_volume, WithinAbs(outside_volume, 1e-12));
    CHECK_THAT(result.missing_volume, WithinAbs(missing_volume, 1e-12));
    CHECK_THAT(result.supply_geometry_residual, WithinAbs(.4, 1e-12));
    CHECK_THAT(result.duplicate_volume, WithinAbs(0., 0.));
    CHECK_THAT(result.core_overlap_volume, WithinAbs(0., 0.));
    const auto& region = result.regions.front();
    CHECK(region.coverage == FM::Coverage::Partial);
    CHECK_THAT(region.target_volume, WithinAbs(2., 1e-12));
    CHECK_THAT(region.covered_volume, WithinAbs(covered_volume, 1e-12));
    CHECK_THAT(region.outside_volume, WithinAbs(outside_volume, 1e-12));
    CHECK_THAT(region.missing_volume, WithinAbs(missing_volume, 1e-12));
    CHECK_THAT(region.supply_geometry_residual, WithinAbs(.4, 1e-12));
    CHECK_FALSE(result.boundary_distance.bounded);
    CHECK(std::isinf(result.boundary_distance.upper_bound));
    CHECK_FALSE(region.boundary_distance.bounded);
    CHECK_FALSE(result.nominal_outer_distance.bounded);
    REQUIRE_FALSE(result.deposited.faces.empty());
    REQUIRE_FALSE(result.missing.faces.empty());
    REQUIRE_FALSE(result.outside.faces.empty());
    CHECK(FG::valid_solid(result.deposited));
    CHECK_THAT(FG::volume(result.deposited), WithinAbs(cell_volume, 1e-12));
    CHECK_THAT(FG::volume(result.missing), WithinAbs(missing_volume, 1e-12));
    CHECK_THAT(FG::volume(result.outside), WithinAbs(outside_volume, 1e-12));
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
