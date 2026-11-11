#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixHybrid.hpp"
#include "libslic3r/FormorixDeposition.hpp"
#include "libslic3r/FormorixGeometry.hpp"
#include "libslic3r/FormorixProcess.hpp"
#include "libslic3r/FormorixPreview.hpp"
#include "libslic3r/FormorixSurfaces.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Format/ProjectStrategy.hpp"
#include "libslic3r/Point.hpp"
#include "test_utils.hpp"
#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
namespace FH = FormorixHybrid;
namespace FG = FormorixGeometry;
namespace PS = FormorixProcess;
namespace FS = FormorixSurfaces;
using Catch::Matchers::WithinAbs;
namespace {
PS::Profile profile(int head = 0)
{
    PS::Profile p;
    p.material_id     = std::array<const char*, 3>{"PETG", "PVA", "PETG-CF"}[head];
    p.nozzle_profile  = "hybrid-nozzle-" + std::to_string(head);
    p.nozzle_diameter = std::array<double, 3>{.8, .4, .6}[head];
    p.line_width      = p.nozzle_diameter * 1.1;
    p.layer_height    = .2;
    return p;
}
FG::Mesh pack(const FG::Mesh& source)
{
    FG::Mesh out;
    std::map<int, int> ids;
    for (const auto& face : source.faces) {
        Vec3i32 f;
        for (int k = 0; k < 3; ++k) {
            auto [it, added] = ids.emplace(face[k], int(out.vertices.size()));
            if (added)
                out.vertices.push_back(source.vertices[face[k]]);
            f[k] = it->second;
        }
        out.faces.push_back(f);
    }
    return out;
}
FH::Input fixture(int shape, bool split = false, bool whole = false)
{
    FH::Input input;
    constexpr int n = 8;
    auto height     = [shape](double x, double y) {
        if (shape == 1)
            return 5. + .25 * x; // variable wall / inclined patch
        if (shape == 2)
            return 4. + std::sqrt(25. - x * x) - .025 * y * y;
        if (shape == 3)
            return 7. - std::sqrt(25. - x * x) + .025 * y * y;
        if (shape == 4) {
            // Negative NACA-0012 mould surface, plus spanwise curvature.
            const double u    = (x + 4.) / 8.;
            const double foil = 8. * .6 * (.2969 * std::sqrt(u) - .126 * u - .3516 * u * u + .2843 * u * u * u - .1036 * u * u * u * u);
            return 5. - foil + .025 * y * y;
        }
        return 5.;
    };
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            const double x = -3. + 6. * i / n, y = -3. + 6. * j / n;
            input.solid.vertices.emplace_back(x, y, height(x, y));
        }
    FG::Mesh patches[2];
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const int a = j * (n + 1) + i, b = a + 1, c = a + n + 1, d = c + 1;
            for (const Vec3i32& f : {Vec3i32(a, b, d), Vec3i32(a, d, c)}) {
                input.solid.faces.push_back(f);
                if (whole || (i >= 2 && i < 6 && j >= 2 && j < 6))
                    patches[split && i >= 4 ? 1 : 0].faces.push_back(f);
            }
        }
    std::vector<int> ring;
    for (int i = 0; i <= n; ++i)
        ring.push_back(i);
    for (int j = 1; j <= n; ++j)
        ring.push_back(j * (n + 1) + n);
    for (int i = n - 1; i >= 0; --i)
        ring.push_back(n * (n + 1) + i);
    for (int j = n - 1; j > 0; --j)
        ring.push_back(j * (n + 1));
    const int start = int(input.solid.vertices.size());
    for (int i : ring)
        input.solid.vertices.emplace_back(input.solid.vertices[i].x(), input.solid.vertices[i].y(), 0.);
    const int center = int(input.solid.vertices.size());
    input.solid.vertices.emplace_back(0., 0., 0.);
    for (int i = 0; i < int(ring.size()); ++i) {
        const int j = (i + 1) % int(ring.size());
        input.solid.faces.emplace_back(ring[i], start + i, start + j);
        input.solid.faces.emplace_back(ring[i], start + j, ring[j]);
        input.solid.faces.emplace_back(center, start + j, start + i);
    }
    for (int h = 0; h < (split ? 2 : 1); ++h) {
        patches[h].vertices = input.solid.vertices;
        FH::RegionInput r;
        r.id                    = h + 1;
        r.head                  = h;
        r.process               = profile(h);
        r.surface               = pack(patches[h]);
        r.settings.layers       = 2;
        r.settings.layer_height = .2;
        r.settings.head_mask    = 1u << h;
        if (shape == 0)
            r.cad_reference = FG::plane({0., 0., 5.}, Vec3d::UnitZ());
        if (shape == 1)
            r.cad_reference = FG::plane({0., 0., 5.}, Vec3d(-.25, 0., 1.).normalized());
        input.regions.push_back(std::move(r));
    }
    return input;
}
void metrics(const char* name, const FH::Result& result)
{
    const auto& material = result.material;
    std::cout << "MATERIAL3D " << name << ",status=" << int(material.status) << ",supplied=" << std::setprecision(17)
              << material.supplied_volume << ",union=" << material.union_volume << ",missing=" << material.missing_volume
              << ",outside=" << material.outside_volume << ",duplicate=" << material.duplicate_volume
              << ",core_overlap=" << material.core_overlap_volume << ",supply_residual=" << material.supply_geometry_residual
              << ",maximum=" << material.boundary_distance.maximum << ",mean=" << material.boundary_distance.mean
              << ",rms=" << material.boundary_distance.rms << ",upper=" << material.boundary_distance.upper_bound
              << ",bounded=" << material.boundary_distance.bounded << ",cells=" << material.cells
              << ",queries=" << material.distance_queries << ",error=" << material.error << "\n";
    for (const auto& r : material.regions)
        std::cout << "MATERIAL3D_REGION " << name << ",region=" << r.region << ",coverage=" << int(r.coverage)
                  << ",missing=" << r.missing_volume << ",outside=" << r.outside_volume
                  << ",supply_residual=" << r.supply_geometry_residual << ",boundary_bounded=" << r.boundary_distance.bounded
                  << ",outer_bounded=" << r.nominal_outer_distance.bounded << ",outer_upper=" << r.nominal_outer_distance.upper_bound
                  << ",input_rounding=" << r.input_rounding_volume_bound << '/' << material.input_rounding_volume_bound
                  << ",zero_checks=" << (r.outside_volume == 0.) << '/' << (material.duplicate_volume == 0.) << '/' << (material.core_overlap_volume == 0.) << "\n";
    for (const auto& r : result.regions) {
        std::cout << "ACCURACY " << name << ",region=" << r.id << ",topology=" << result.partition.output_topology_preserved
                  << ",bounds=" << r.accuracy.bounds[0] << '/' << r.accuracy.bounds[1] << '/' << r.accuracy.bounds[2] << '/'
                  << r.accuracy.bounds[3] << '/' << r.accuracy.bounds[4] << ",inadmissible=" << r.inadmissible_material
                  << ",volume_plausible=" << r.accuracy.material_volume_plausible << "\n";
        double timeline_volume = 0.;
        for (const auto& track : result.preview.timeline.heads)
            for (const auto& motion : track)
                if (motion.region == r.id) {
                    const double length = (motion.to.position - motion.from.position).norm();
                    if (motion.extrusion) {
                        timeline_volume += length * motion.mm3_per_mm;
                        REQUIRE(motion.process);
                        CHECK(length * motion.mm3_per_mm / (motion.end - motion.start) <= motion.process->volumetric_limit + 1e-7);
                    } else
                        CHECK_THAT(motion.mm3_per_mm, WithinAbs(0., 1e-12));
                }
        CHECK_THAT(timeline_volume, WithinAbs(r.deposited_volume, 1e-8 * std::max(1., r.deposited_volume)));
        std::cout << "HYBRID " << name << "," << r.id << "," << std::setprecision(17) << result.partition.volume_residual << ","
                  << r.accuracy.bounds[2] << "," << r.coverage.sampled_maximum << "," << r.coverage.sampled_mean << ","
                  << r.coverage.sampled_rms << "," << r.coverage.uncovered_fraction << "," << r.coverage.outside_fraction << ","
                  << r.volume_ratio << "," << r.accuracy.verified() << "\n";
    }
}
TEST_CASE("A commensurate planar ribbon exactly partitions finite cells without changing flow", "[FormorixHybrid][RibbonBaseline]")
{
    auto input                          = fixture(0);
    input.regions[0].process.line_width = .75 + .2 * (1. - std::acos(-1.) / 4.);
    const auto before                   = input.regions[0].process;
    const auto result                   = FH::generate(input);
    INFO(result.error);
    REQUIRE(result.ok());
    const auto& r = result.regions[0];
    CHECK_THAT(r.compacted_width, WithinAbs(.75, 1e-12));
    CHECK_THAT(r.volume_ratio, WithinAbs(1., 1e-8));
    CHECK_THAT(r.projected_overlap_volume, WithinAbs(0., 1e-8));
    CHECK_THAT(r.projected_outside_volume, WithinAbs(0., 1e-8));
    CHECK_THAT(r.projected_uncovered_volume, WithinAbs(0., 1e-8));
    CHECK_THAT(r.coverage.sampled_maximum, WithinAbs(0., 1e-8));
    CHECK(r.accuracy.material_partition_verified);
    CHECK(r.accuracy.verified());
    CHECK(r.accuracy.bounds[2] >= FH::to_float(result.core).displacement_bound);
    CHECK_THAT(input.regions[0].process.flow_ratio, WithinAbs(before.flow_ratio, 1e-12));
    CHECK_THAT(input.regions[0].process.line_width, WithinAbs(before.line_width, 1e-12));
    metrics("commensurate_plane", result);
}
TEST_CASE("A complete fine outer layer cannot certify missing inner ribbon cells", "[FormorixHybrid]")
{
    auto input                                  = fixture(0);
    input.regions[0].process.line_width         = .75 + .1 * (1. - std::acos(-1.) / 4.);
    input.regions[0].settings.fine_final_layers = 1;
    input.regions[0].settings.fine_layer_height = .1;
    const auto result                           = FH::generate(input);
    INFO(result.error);
    REQUIRE(result.ok());
    CHECK(result.regions[0].projected_uncovered_volume > 0.);
    CHECK_FALSE(result.regions[0].accuracy.material_partition_verified);
    CHECK_FALSE(result.regions[0].accuracy.verified());
}
TEST_CASE("Explicit local width limits fill a noncommensurate rectangle without changing flow", "[FormorixHybrid][BoundaryFill]")
{
    auto input = fixture(0, false, true);
    for (auto& vertex : input.solid.vertices) {
        vertex.x() *= 2.;
        vertex.y() *= 6.1 / 6.;
    }
    for (auto& vertex : input.regions[0].surface.vertices) {
        vertex.x() *= 2.;
        vertex.y() *= 6.1 / 6.;
    }
    auto& p = input.regions[0].process;
    p.line_width = .75 + .2 * (1. - std::acos(-1.) / 4.);
    p.local_width_limits = PS::WidthLimits{.74, .84};
    p.volumetric_limit = .7;
    const auto before = p;
    const auto result = FH::generate(input);
    INFO(result.error);
    INFO(result.material.error);
    REQUIRE(result.ok());
    REQUIRE(result.material.ok());
    CHECK(result.regions[0].adaptive_boundary);
    CHECK_THAT(result.regions[0].minimum_local_width, WithinAbs(.7625 + .2 * (1. - std::acos(-1.) / 4.), 1e-12));
    CHECK(result.regions[0].accuracy.verified());
    CHECK_THAT(result.material.union_volume, WithinAbs(12. * 6.1 * .4, 1e-10));
    CHECK_THAT(result.material.missing_volume, WithinAbs(0., 0.));
    CHECK_THAT(result.material.outside_volume, WithinAbs(0., 0.));
    CHECK_THAT(result.material.duplicate_volume, WithinAbs(0., 0.));
    for (const auto& motion : result.preview.timeline.heads[0]) {
        if (!motion.extrusion)
            continue;
        REQUIRE(motion.process.has_value());
        CHECK_THAT(motion.process->line_width, WithinAbs(result.regions[0].minimum_local_width, 1e-12));
        CHECK_THAT(motion.process->flow_ratio, WithinAbs(before.flow_ratio, 0.));
        const double speed = (motion.to.position - motion.from.position).norm() / (motion.end - motion.start);
        CHECK(speed * motion.mm3_per_mm <= before.volumetric_limit + 1e-10);
    }
    CHECK_THAT(p.line_width, WithinAbs(before.line_width, 0.));
    CHECK_THAT(p.flow_ratio, WithinAbs(before.flow_ratio, 0.));
    metrics("adaptive_rectangle", result);
}
TEST_CASE("A fixed profile leaves an explicit noncommensurate boundary remainder", "[FormorixHybrid][BoundaryFill]")
{
    auto input = fixture(0);
    input.regions[0].process.line_width = .7 + .2 * (1. - std::acos(-1.) / 4.);
    const auto result = FH::generate(input);
    INFO(result.error);
    REQUIRE(result.ok());
    CHECK_FALSE(result.regions[0].adaptive_boundary);
    CHECK(result.regions[0].maximum_boundary_remainder > 0.);
    CHECK_FALSE(result.regions[0].accuracy.verified());
    CHECK(result.material.missing_volume > 0.);
    CHECK_THAT(result.regions[0].minimum_local_width, WithinAbs(input.regions[0].process.line_width, 0.));
    metrics("fixed_boundary_remainder", result);
}
TEST_CASE("Every hybrid deposition run retains its actual layer head material and supplied volume", "[FormorixHybrid]")
{
    auto input = fixture(0, true);
    const auto original = input.solid;
    const auto result = FH::generate(input);
    INFO(result.error);
    REQUIRE(result.ok());
    REQUIRE_FALSE(result.material_runs.empty());
    std::array<double, 3> supply{};
    for (const auto& run : result.material_runs) {
        REQUIRE(run.head >= 0);
        REQUIRE(run.head < 3);
        REQUIRE(run.stations.size() > 1);
        REQUIRE(run.supplied_volumes.size() + 1 == run.stations.size());
        REQUIRE(run.layer >= 0);
        REQUIRE(std::size_t(run.layer) < result.preview.layers.size());
        CHECK(run.material == input.regions[run.head].process.material_id);
        CHECK(run.region == input.regions[run.head].id);
        for (double volume : run.supplied_volumes) {
            CHECK(volume > 0.);
            supply[run.head] += volume;
        }
    }
    for (const auto& region : result.regions)
        CHECK_THAT(supply[region.head], WithinAbs(region.deposited_volume, 1e-10));
    CHECK(input.solid.vertices == original.vertices);
    CHECK(input.solid.faces == original.faces);
}
TEST_CASE("An incomplete spatial audit cannot inherit a positive planar projection certificate", "[FormorixHybrid]")
{
    auto input = fixture(0);
    input.regions[0].process.line_width = .75 + .2 * (1. - std::acos(-1.) / 4.);
    input.material_options.max_stations = 2;
    const auto result = FH::generate(input);
    INFO(result.error);
    REQUIRE(result.ok()); // Existing paths remain diagnostic; no material approval.
    CHECK_FALSE(result.material.ok());
    CHECK_FALSE(result.regions[0].accuracy.material_partition_verified);
    CHECK_FALSE(result.regions[0].accuracy.verified());
    CHECK_FALSE(std::isfinite(result.regions[0].accuracy.bounds[4]));
}
TEST_CASE("Different nozzle ribbons meet at an identical planar material boundary without geometric gaps",
          "[FormorixHybrid][RibbonBaseline]")
{
    auto input                          = fixture(0, true);
    input.regions[0].process.line_width = .75 + .2 * (1. - std::acos(-1.) / 4.);
    input.regions[1].process.line_width = .375 + .2 * (1. - std::acos(-1.) / 4.);
    input.regions[1].settings.layers    = 3;
    const auto result                   = FH::generate(input);
    INFO(result.error);
    REQUIRE(result.ok());
    REQUIRE(result.regions.size() == 2);
    CHECK(std::abs(result.regions[0].process.nozzle_diameter - result.regions[1].process.nozzle_diameter) > .1);
    CHECK(result.regions[0].process.material_id != result.regions[1].process.material_id);
    for (const auto& region : result.regions) {
        CHECK_THAT(region.volume_ratio, WithinAbs(1., 1e-8));
        CHECK_THAT(region.projected_overlap_volume, WithinAbs(0., 1e-8));
        CHECK_THAT(region.projected_uncovered_volume, WithinAbs(0., 1e-8));
        CHECK_THAT(region.projected_outside_volume, WithinAbs(0., 1e-8));
        CHECK(region.accuracy.material_partition_verified);
        CHECK(region.accuracy.verified());
    }
    const auto seam = std::find_if(result.partition.interfaces.begin(), result.partition.interfaces.end(),
                                   [](const FG::Interface& interface) { return interface.first == 1 && interface.second == 2; });
    REQUIRE(seam != result.partition.interfaces.end());
    REQUIRE_FALSE(seam->mesh.faces.empty());
    for (const auto& p : seam->mesh.vertices)
        CHECK_THAT(p.x(), WithinAbs(0., 1e-9));
    metrics("mixed_nozzle_commensurate", result);
}
TEST_CASE("Timeline supply validation rejects invalid numbers and extrusion on travel", "[FormorixHybrid]")
{
    FormorixPreview::Timeline timeline;
    FormorixPreview::Motion motion;
    motion.head       = 0;
    motion.start      = 0.;
    motion.end        = 1.;
    motion.from       = {{0., 0., 0.}, Vec3d::UnitZ()};
    motion.to         = {{1., 0., 0.}, Vec3d::UnitZ()};
    motion.mm3_per_mm = .1;
    timeline.heads[0].push_back(motion);
    REQUIRE(timeline.valid());
    for (double invalid : {-1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        timeline.heads[0][0].mm3_per_mm = invalid;
        CHECK_FALSE(timeline.valid());
    }
    timeline.heads[0][0].mm3_per_mm = 0.;
    CHECK(timeline.valid()); // Legacy motion has no explicit volumetric density.
    timeline.heads[0][0].kind      = FormorixPreview::MotionKind::Travel;
    timeline.heads[0][0].extrusion = false;
    CHECK(timeline.valid());
    timeline.heads[0][0].mm3_per_mm = .1;
    CHECK_FALSE(timeline.valid());
}
TEST_CASE("Curved ribbon timing preserves supply when the TCP has a contact offset", "[FormorixHybrid]")
{
    auto input                              = fixture(2);
    input.regions[0].process.contact_to_tcp = 7.;
    const auto result                       = FH::generate(input);
    INFO(result.error);
    REQUIRE(result.ok());
    metrics("curved_tcp_offset", result);
}
TEST_CASE("Fine surface quality increases longitudinal resolution without changing material supply or pitch", "[FormorixHybrid]")
{
    auto input                        = fixture(2);
    input.regions[0].settings.quality = 0;
    const auto draft                  = FH::generate(input);
    INFO(draft.error);
    REQUIRE(draft.ok());
    input.regions[0].settings.quality = 2;
    const auto fine                   = FH::generate(input);
    INFO(fine.error);
    REQUIRE(fine.ok());
    auto points = [](const FH::Result& result) {
        std::size_t count = 0;
        for (const auto& layer : result.preview.layers)
            count += layer.path.size();
        return count;
    };
    CHECK(points(fine) > points(draft));
    CHECK_THAT(fine.regions[0].compacted_width, WithinAbs(draft.regions[0].compacted_width, 1e-12));
    CHECK_THAT(fine.regions[0].supplied_area, WithinAbs(draft.regions[0].supplied_area, 1e-12));
    CHECK_THAT(fine.regions[0].process.flow_ratio, WithinAbs(draft.regions[0].process.flow_ratio, 1e-12));
    CHECK_THAT(fine.regions[0].process.line_width, WithinAbs(draft.regions[0].process.line_width, 1e-12));
    CHECK_FALSE(draft.regions[0].accuracy.verified());
    CHECK_FALSE(fine.regions[0].accuracy.verified());
    std::cout << "RIBBON_QUALITY draft=" << points(draft) << " fine=" << points(fine)
              << " unchanged_pitch=" << fine.regions[0].compacted_width << "\n";
}
TEST_CASE("The original surface preview also applies quality only to longitudinal samples", "[FormorixHybrid]")
{
    const auto model = fixture(0);
    const FormorixPreview::Input input{FH::to_float(model.regions[0].surface).mesh, FH::to_float(model.solid).mesh, {}};
    auto settings    = model.regions[0].settings;
    settings.quality = 0;
    const FormorixPreview::Options options;
    const auto draft = FormorixPreview::generate(input, settings, options);
    REQUIRE(draft.ok());
    settings.quality = 2;
    const auto fine  = FormorixPreview::generate(input, settings, options);
    REQUIRE(fine.ok());
    CHECK(fine.layers.front().path.size() > draft.layers.front().path.size());
    CHECK_THAT(fine.shell_depth, WithinAbs(draft.shell_depth, 1e-12));
    CHECK(fine.layers.size() == draft.layers.size());
}
} // namespace
TEST_CASE("Hybrid partition preserves a planar reference and exposes an unfilled final ribbon strip", "[FormorixHybrid]")
{
    const auto input  = fixture(0);
    const auto before = input.solid.vertices;
    const auto result = FH::generate(input);
    INFO(result.error);
    REQUIRE(result.ok());
    CHECK(input.solid.vertices == before);
    CHECK(FG::valid_solid(result.core));
    CHECK_THAT(result.partition.volume_residual, WithinAbs(0., 1e-7));
    REQUIRE(result.regions.size() == 1);
    const auto& r = result.regions.front();
    CHECK(r.accuracy.bounds[0] < .01);
    CHECK(r.accuracy.bounds[1] < .01);
    CHECK(r.accuracy.bounds[2] < .01);
    CHECK(r.accuracy.bounds[3] < .01);
    CHECK(r.volume_ratio < 1.1);
    CHECK_FALSE(r.accuracy.material_volume_plausible);
    CHECK_FALSE(r.accuracy.verified());
    CHECK(result.preview.timeline.valid());
    REQUIRE(result.preview.layers.size() == 2);
    CHECK_THAT(result.preview.layers.back().path[0].position.z(), WithinAbs(4.9, 1e-6));
    CHECK_THAT(result.preview.timeline.heads[0].back().to.position.z(), WithinAbs(5., 1e-6));
    for (size_t layer = 0; layer < result.preview.layers.size(); ++layer) {
        size_t count = 0;
        for (const auto& move : result.preview.timeline.heads[0])
            if (move.layer == int(layer) && (move.to.position - move.from.position).norm() > 0. &&
                move.kind != FormorixPreview::MotionKind::Wait)
                ++count;
        // Inter-layer travel is an extra motion outside the layer path.
        const auto& path = result.preview.layers[layer];
        CHECK(path.extruding.size() + 1 == path.path.size());
        CHECK(count >= path.extruding.size());
    }
    metrics("plane", result);
}
TEST_CASE("Hybrid shells process inclined convex concave and hydrofoil negative surfaces conservatively", "[FormorixHybrid]")
{
    const int shape   = GENERATE(1, 2, 3, 4);
    auto input        = fixture(shape);
    const auto result = FH::generate(input);
    INFO("shape=" << shape << " " << result.error);
    REQUIRE(result.ok());
    CHECK(FG::valid_solid(result.core));
    CHECK_THAT(result.partition.volume_residual, WithinAbs(0., 1e-6));
    CHECK(result.preview.timeline.valid());
    CHECK_FALSE(result.regions.front().accuracy.verified());
    if (shape >= 2) {
        CHECK_FALSE(result.regions.front().coverage.bounded);
        CHECK_FALSE(std::isfinite(result.regions.front().accuracy.bounds[0]));
    }
    const char* names[] = {"plane", "inclined_variable_wall", "convex", "concave", "hydrofoil_negative"};
    metrics(names[shape], result);
}
TEST_CASE("Adjacent hybrid regions retain their own heads widths and materials without a scheduler", "[FormorixHybrid]")
{
    auto input                       = fixture(0, true);
    input.regions[1].settings.layers = 3;
    const auto result                = FH::generate(input);
    INFO(result.error);
    REQUIRE(result.ok());
    REQUIRE(result.regions.size() == 2);
    CHECK(result.partition.interfaces.size() == 3);
    CHECK(result.regions[0].process.material_id == "PETG");
    CHECK(result.regions[1].process.material_id == "PVA");
    CHECK_THAT(result.regions[0].process.nozzle_diameter, WithinAbs(.8, 1e-12));
    CHECK_THAT(result.regions[1].process.nozzle_diameter, WithinAbs(.4, 1e-12));
    REQUIRE_FALSE(result.preview.timeline.heads[0].empty());
    REQUIRE_FALSE(result.preview.timeline.heads[1].empty());
    CHECK(result.preview.timeline.heads[1].front().start >= result.preview.timeline.heads[0].back().end);
    metrics("adjacent_materials", result);
}
TEST_CASE("A rigid tilted section is rejected instead of intersecting the conventional core", "[FormorixHybrid]")
{
    auto input = fixture(0);
    // Exact coordinate permutation avoids inventing CAD rotation rounding in this reference.
    auto rotate = [](Vec3d& p) {
        const double x = p.x();
        p.x()          = p.z();
        p.z()          = -x + 3.;
    };
    for (auto& p : input.solid.vertices)
        rotate(p);
    for (auto& p : input.regions[0].surface.vertices)
        rotate(p);
    input.regions[0].cad_reference = FG::plane({5., 0., 0.}, Vec3d::UnitX());
    CHECK_FALSE(FH::generate(input).ok());
    input.regions[0].settings.allow_vertical  = true;
    input.regions[0].settings.allow_deviation = true;
    auto result                               = FH::generate(input);
    INFO(result.error);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error.find("perpendicular nozzle") != std::string::npos);
    CHECK(result.preview.layers.empty());
}
TEST_CASE("Saved surface paint extracts all regions without changing the original object", "[FormorixHybrid]")
{
    auto input = fixture(0);
    Model model;
    auto* object = model.add_object();
    auto* volume = object->add_volume(TriangleMesh(FH::to_float(input.solid).mesh));
    object->add_instance();
    TriangleSelector selector(volume->mesh());
    selector.set_facet(0, EnforcerBlockerType(1));
    selector.set_facet(1, EnforcerBlockerType(2));
    volume->formorix_facets.set(selector);
    volume->formorix_topology = FS::topology_id(volume->mesh().its);
    const auto original       = volume->mesh().its.vertices;
    FS::Project project;
    project.regions                       = {{1, "First"}, {2, "Hidden"}};
    project.regions[1].visible            = false;
    project.regions[1].settings.head_mask = 4;
    std::array<PS::Profile, 3> profiles{profile(0), profile(1), profile(2)};
    const auto extracted = FH::extract(*object, 0, project, profiles);
    REQUIRE(extracted.regions.size() == 2);
    CHECK(extracted.regions[1].head == 2);
    CHECK(extracted.regions[1].process.material_id == "PETG-CF");
    CHECK(volume->mesh().its.vertices == original);
}
TEST_CASE("Hybrid evidence distinguishes missing underlay unknown CAD and lossy float conversion", "[FormorixHybrid]")
{
    auto result = FH::generate(fixture(0));
    REQUIRE(result.ok());
    FH::audit_underlay(result, {});
    CHECK(result.regions[0].underlay.missing == result.regions[0].underlay.samples);
    CHECK_FALSE(result.missing_underlay.empty());
    CHECK_FALSE(result.regions[0].underlay.coverage_bounded);
    FH::Result local;
    FH::RegionResult region;
    region.process           = profile();
    region.interface_samples = {{0., 0., 4.}, {0., 2., 4.}};
    region.interface_normals = {Vec3d::UnitZ(), Vec3d::UnitZ()};
    local.regions.push_back(region);
    FH::audit_underlay(local, {{{-1., 0., 4.}, {1., 0., 4.}, .8, .2, 0, "PETG", false}});
    CHECK(local.regions[0].underlay.samples == 2);
    CHECK(local.regions[0].underlay.missing == 1);
    local.regions[0].settings.use_infill         = false;
    local.regions[0].settings.dense_underlay     = true;
    local.regions[0].settings.additional_support = true;
    local.regions[0].settings.support_material   = FS::SupportMaterial::Soluble;
    FH::audit_underlay(local, {{{-1., 0., 4.}, {1., 0., 4.}, .8, .2, 0, "PETG", false, true}});
    CHECK(local.regions[0].underlay.missing == 2);
    const auto has_warning = [&](const char* text) {
        return std::any_of(local.warnings.begin(), local.warnings.end(),
                           [&](const std::string& warning) { return warning.find(text) != std::string::npos; });
    };
    CHECK(has_warning("dense transition underlay is not generated"));
    CHECK(has_warning("additional 5D support is not generated"));
    CHECK(has_warning("soluble support material suitability"));
    FG::Mesh points;
    points.vertices      = {{1600.00001, 0., 0.}};
    const auto converted = FH::to_float(points);
    CHECK(converted.displacement_bound > 0.);
    CHECK(converted.displacement_bound < .01);
    auto input = fixture(0);
    input.regions[0].cad_reference.reset();
    auto unknown = FH::generate(input);
    REQUIRE(unknown.ok());
    CHECK_FALSE(unknown.regions[0].accuracy.verified());
    CHECK_FALSE(std::isfinite(unknown.regions[0].accuracy.bounds[0]));
}
TEST_CASE("Hybrid extraction rejects model semantics which the rebuilt core cannot preserve", "[FormorixHybrid]")
{
    Model model;
    auto* object = model.add_object();
    auto* volume = object->add_volume(TriangleMesh(FH::to_float(fixture(0).solid).mesh));
    object->add_instance();
    FS::Project project;
    const std::array<PS::Profile, 3> profiles{profile(0), profile(1), profile(2)};
    object->config.set_key_value("formorix_5d_enabled", new ConfigOptionBool(true));
    REQUIRE_NOTHROW(FH::extract(*object, 0, project, profiles));
    volume->set_type(ModelVolumeType::NEGATIVE_VOLUME);
    CHECK_THROWS(FH::extract(*object, 0, project, profiles));
    volume->set_type(ModelVolumeType::MODEL_PART);
    object->config.set_key_value("layer_height", new ConfigOptionFloat(.3));
    CHECK_THROWS(FH::extract(*object, 0, project, profiles));
    object->config.erase("layer_height");
    volume->config.set_key_value("extruder", new ConfigOptionInt(2));
    CHECK_THROWS(FH::extract(*object, 0, project, profiles));
    volume->config.erase("extruder");
    object->layer_config_ranges[{0., 2.}].set_key_value("layer_height", new ConfigOptionFloat(.3));
    CHECK_THROWS(FH::extract(*object, 0, project, profiles));
    object->layer_config_ranges.clear();
    auto input                                      = fixture(0);
    input.regions[0].process.shrink_compensation[0] = 1.02;
    const auto result                               = FH::generate(input);
    CHECK_FALSE(result.ok());
    CHECK(result.error.find("neutral material shrink") != std::string::npos);
}
TEST_CASE("Missing nominal envelope samples never masquerade as zero geometric error", "[FormorixHybrid]")
{
    auto input                                = fixture(0);
    input.regions[0].settings.boundary_margin = .8;
    const auto result                         = FH::generate(input);
    INFO(result.error);
    REQUIRE(result.ok());
    const auto& coverage = result.regions[0].coverage;
    CHECK(coverage.samples > 0);
    CHECK(coverage.missing_samples > 0);
    CHECK_FALSE(std::isfinite(coverage.sampled_maximum));
    CHECK_FALSE(std::isfinite(coverage.sampled_mean));
    CHECK_FALSE(std::isfinite(coverage.sampled_rms));
    CHECK_FALSE(result.regions[0].accuracy.verified());
}
TEST_CASE("Impossible hybrid partitions and canceled computations keep no accepted partial preview", "[FormorixHybrid]")
{
    auto input                       = fixture(0);
    input.regions[0].settings.layers = 100;
    auto result                      = FH::generate(input);
    CHECK_FALSE(result.ok());
    CHECK(result.preview.layers.empty());
    input = fixture(0);
    input.regions.push_back(input.regions.front());
    input.regions.back().id = 2;
    result                  = FH::generate(input);
    CHECK_FALSE(result.ok());
    CHECK(result.partition.parts.empty());
    CHECK_FALSE(FH::generate(fixture(0), [] { return true; }).ok());
    input                                = fixture(1);
    input.regions[0].settings.shell_mode = FS::ShellMode::FullWall;
    CHECK_FALSE(FH::generate(input).ok());
}
TEST_CASE("Invalid head and region identities are rejected before constructing indexed tracks", "[FormorixHybrid]")
{
    auto input = fixture(0);
    input.regions[0].head = GENERATE(-1, 3, 99);
    CHECK_FALSE(FH::generate(input).ok());
    input.regions[0].head = 0;
    input.regions[0].id = 0;
    CHECK_FALSE(FH::generate(input).ok());
    input.regions[0].id = 1;
    input.regions.push_back(input.regions[0]);
    CHECK_FALSE(FH::generate(input).ok());
}
TEST_CASE("Hybrid expansion bounds the final movement count across all layers", "[FormorixHybrid]")
{
    auto input = fixture(0);
    // Exact planar rows no longer need artificial longitudinal subdivisions.
    // Set the resource ceiling explicitly so this still tests accumulated work,
    // rather than depending on the old representation's redundant points.
    input.max_motions = 200;
    for (auto& p : input.solid.vertices) {
        p.x() *= 20.;
        p.y() *= 20.;
    }
    for (auto& p : input.regions[0].surface.vertices) {
        p.x() *= 20.;
        p.y() *= 20.;
    }
    input.regions[0].settings.layers       = 200;
    input.regions[0].settings.layer_height = .01;
    const auto result                      = FH::generate(input);
    INFO(result.error);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error.find("movement budget exceeded") != std::string::npos);
    CHECK(result.preview.layers.empty());
    CHECK(result.partition.parts.empty());
}
TEST_CASE("Whole top faces either retain internal material centers or explicitly require boundary termination", "[FormorixHybrid]")
{
    const int shape = GENERATE(1, 2, 3, 4);
    auto result     = FH::generate(fixture(shape, false, true));
    INFO("shape=" << shape << " " << result.error);
    REQUIRE(result.ok());
    CHECK(FG::valid_solid(result.core));
    for (const auto& layer : result.preview.layers)
        CHECK(layer.extruding.size() + 1 == layer.path.size());
    metrics("complete_top_boundary", result);
}
TEST_CASE("The saved curved-wall project generates a hybrid preview from its real paint", "[FormorixHybrid]")
{
    ScopedSlic3rTemporaryDir temporary("formorix-hybrid-project");
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
    auto input = FH::extract(*model.objects.front(), 0, *project, {profile(0), profile(1), profile(2)});
    REQUIRE(input.regions.size() == 2);
    auto result = FH::generate(input);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error.find("perpendicular nozzle") != std::string::npos);
    std::cout << "HYBRID native_curved_wall_with_side," << result.error << "\n";
    // The supplied editor demonstration also marks a perpendicular wall with
    // a different depth. Its rigid tilted material section is rejected above;
    // the shared skin partition is independently checked by geometry tests.
    // Check the original curved marking alone without changing its triangles.
    input.regions.resize(1);
    result = FH::generate(input);
    INFO(result.error);
    REQUIRE(result.ok());
    CHECK(FG::valid_solid(result.core));
    metrics("native_curved_wall", result);
    // The actual Formorix default resolves .4/.42 rather than the .8/.88
    // analytical fixture. Its narrow transverse pitch must not force equally
    // narrow longitudinal samples before adaptive compensation.
    input.regions[0].process.nozzle_diameter = .4;
    input.regions[0].process.line_width      = .42;
    input.regions[0].process.flow_ratio      = .98;
    input.regions[0].process.speed           = 60.;
    result                                   = FH::generate(input);
    INFO(result.error);
    REQUIRE(result.ok());
    CHECK(FG::valid_solid(result.core));
    CHECK(result.preview.timeline.valid());
    std::size_t movements = 0;
    for (const auto& track : result.preview.timeline.heads)
        movements += track.size();
    CHECK(movements <= 500000);
    CHECK(result.preview.layers.size() == 4);
    CHECK(result.regions[0].volume_ratio < 1.1);
    CHECK(result.regions[0].volume_ratio > .9);
    CHECK_THAT(input.regions[0].process.flow_ratio, WithinAbs(.98, 1e-12));
    metrics("native_curved_wall_standard04", result);
}

TEST_CASE("A uniform full-wall slab has no conventional core and still requires prior underlay", "[FormorixHybrid][FormorixHybridFullWall]")
{
    // Analytic 4 x 4 x 1 mm slab. The complete top face owns the entire
    // constant-thickness wall along +Z; there is no residual conventional volume.
    FH::Input input;
    input.solid.vertices = {{0., 0., 0.}, {4., 0., 0.}, {4., 4., 0.}, {0., 4., 0.}, {0., 0., 1.}, {4., 0., 1.}, {4., 4., 1.}, {0., 4., 1.}};
    input.solid.faces    = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                            {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
    FH::RegionInput region;
    region.id                      = 1;
    region.head                    = 0;
    region.surface.vertices        = {{0., 0., 1.}, {4., 0., 1.}, {4., 4., 1.}, {0., 4., 1.}};
    region.surface.faces           = {{0, 1, 2}, {0, 2, 3}};
    region.settings.shell_mode     = FormorixSurfaces::ShellMode::FullWall;
    region.settings.layer_height   = .2;
    region.process.material_id     = "PETG";
    region.process.nozzle_profile  = "Analytic full-wall nozzle";
    region.process.nozzle_diameter = .4;
    region.process.line_width      = .44;
    region.process.layer_height    = .2;
    region.cad_reference           = FG::plane({0., 0., 1.}, Vec3d::UnitZ());
    input.regions.push_back(std::move(region));
    REQUIRE(FG::valid_solid(input.solid));

    auto result = FH::generate(input);
    INFO(result.error);
    REQUIRE(result.ok());
    CHECK(result.core.faces.empty());
    CHECK(result.core.vertices.empty());
    CHECK(result.preview.core.indices.empty());
    REQUIRE(result.partition.parts.size() == 1);
    CHECK(result.partition.parts[0].region == 1);
    CHECK(FG::valid_solid(result.partition.parts[0].mesh));
    CHECK_THAT(result.partition.input_volume, WithinAbs(16., 1e-9));
    CHECK_THAT(result.partition.parts[0].volume, WithinAbs(16., 1e-9));
    CHECK_THAT(result.partition.volume_residual, WithinAbs(0., 1e-9));
    CHECK(result.partition.interfaces.empty());
    REQUIRE(result.regions.size() == 1);
    CHECK_THAT(result.regions[0].shell_depth, WithinAbs(1., 1e-12));
    CHECK_THAT(result.regions[0].shell_volume, WithinAbs(16., 1e-9));
    REQUIRE(result.preview.layers.size() == 5);
    for (std::size_t layer = 0; layer < result.preview.layers.size(); ++layer) {
        REQUIRE_FALSE(result.preview.layers[layer].path.empty());
        for (const auto& pose : result.preview.layers[layer].path)
            CHECK_THAT(pose.position.z(), WithinAbs(.1 + .2 * double(layer), 1e-9));
    }
    CHECK(result.preview.timeline.valid());

    // An empty conventional pass supplies no supporting material. Successful
    // geometry must never turn an unsupported full-wall shell into approved work.
    FH::audit_underlay(result, {});
    const auto& underlay = result.regions[0].underlay;
    REQUIRE(underlay.samples > 0);
    CHECK(underlay.missing == underlay.samples);
    CHECK_FALSE(result.missing_underlay.empty());
    CHECK_FALSE(underlay.coverage_bounded);
    CHECK_FALSE(result.regions[0].accuracy.verified());
}
