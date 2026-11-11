#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixHybrid.hpp"
#include "libslic3r/FormorixMaterial.hpp"
#include "libslic3r/FormorixGeometry.hpp"
#include "libslic3r/FormorixProcess.hpp"
#include "libslic3r/FormorixPreview.hpp"
#include "libslic3r/FormorixSurfaces.hpp"
#include "libslic3r/Point.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
namespace FH = FormorixHybrid;
namespace FM = FormorixMaterial;
namespace FG = FormorixGeometry;
namespace PS = FormorixProcess;
namespace PV = FormorixPreview;
namespace FS = FormorixSurfaces;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double top_z = 5., region_area = 24.;
const std::array<double, 3> heights{{.25, .125, .1875}};
const std::array<int, 3> layer_counts{{2, 3, 4}};
const std::array<int, 3> row_counts{{5, 10, 6}};

PS::Profile profile(int head)
{
    PS::Profile p;
    p.material_id        = std::array<const char*, 3>{{"A", "B", "C"}}[head];
    p.nozzle_profile     = "independent-nozzle-" + p.material_id;
    p.nozzle_diameter    = std::array<double, 3>{{.8, .4, .6}}[head];
    p.line_width         = std::array<double, 3>{{.9, .45, .7}}[head];
    p.layer_height       = heights[head];
    p.temperature        = std::array<double, 3>{{245., 205., 265.}}[head];
    p.volumetric_limit   = std::array<double, 3>{{.6, .2, .45}}[head];
    p.speed              = std::array<double, 3>{{75., 35., 55.}}[head];
    p.flow_ratio         = std::array<double, 3>{{.96, .92, 1.04}}[head];
    p.print_flow_ratio   = std::array<double, 3>{{1.02, 1.05, .97}}[head];
    p.surface_flow_ratio = std::array<double, 3>{{.98, 1.03, .94}}[head];
    p.contact_to_tcp     = std::array<double, 3>{{2., 5., 9.}}[head];
    p.local_width_limits = std::array<PS::WidthLimits, 3>{{{.8, 1.}, {.4, .5}, {.6, .8}}}[head];
    return p;
}

FH::Input workpiece()
{
    FH::Input input;
    // One closed 18 x 4 x 5 mm body, with conforming top triangles at x=6 and 12.
    for (int i = 0; i <= 3; ++i) {
        const double x = 6. * i;
        input.solid.vertices.insert(input.solid.vertices.end(), {{x, 0., top_z}, {x, 4., top_z}, {x, 0., 0.}, {x, 4., 0.}});
    }
    for (int i = 0; i < 3; ++i) {
        const int a = 4 * i;
        for (const Vec3i32& f : std::array<Vec3i32, 8>{{{a, a + 4, a + 5},
                                                        {a, a + 5, a + 1},
                                                        {a + 2, a + 7, a + 6},
                                                        {a + 2, a + 3, a + 7},
                                                        {a, a + 2, a + 6},
                                                        {a, a + 6, a + 4},
                                                        {a + 1, a + 5, a + 7},
                                                        {a + 1, a + 7, a + 3}}})
            input.solid.faces.push_back(f);
        FH::RegionInput region;
        region.id                    = i + 1;
        region.head                  = i;
        region.process               = profile(i);
        region.surface.vertices      = {{6. * i, 0., top_z}, {6. * (i + 1), 0., top_z}, {6. * (i + 1), 4., top_z}, {6. * i, 4., top_z}};
        region.surface.faces         = {{0, 1, 2}, {0, 2, 3}};
        region.settings.layers       = layer_counts[i];
        region.settings.layer_height = heights[i];
        region.settings.head_mask    = 1u << i;
        region.cad_reference         = FG::plane({0., 0., top_z}, Vec3d::UnitZ());
        input.regions.push_back(std::move(region));
    }
    input.solid.faces.insert(input.solid.faces.end(), {{0, 1, 3}, {0, 3, 2}, {12, 14, 15}, {12, 15, 13}});
    return input;
}

double depth(int head) { return heights[head] * layer_counts[head]; }
double wanted_volume(int head) { return region_area * depth(head); }
double local_width(int head)
{
    const auto p = profile(head);
    return (4. / row_counts[head]) / (p.flow_ratio * p.print_flow_ratio * p.surface_flow_ratio) +
           heights[head] * (1. - std::acos(-1.) / 4.);
}

void check_profile(const PS::Profile& actual, int head, bool local)
{
    const auto expected = profile(head);
    CHECK(actual.material_id == expected.material_id);
    CHECK(actual.nozzle_profile == expected.nozzle_profile);
    CHECK_THAT(actual.nozzle_diameter, WithinAbs(expected.nozzle_diameter, 1e-12));
    CHECK_THAT(actual.line_width, WithinAbs(local ? local_width(head) : expected.line_width, 1e-12));
    CHECK_THAT(actual.layer_height, WithinAbs(expected.layer_height, 1e-12));
    CHECK_THAT(actual.temperature, WithinAbs(expected.temperature, 1e-12));
    CHECK_THAT(actual.volumetric_limit, WithinAbs(expected.volumetric_limit, 1e-12));
    CHECK_THAT(actual.speed, WithinAbs(expected.speed, 1e-12));
    CHECK_THAT(actual.flow_ratio, WithinAbs(expected.flow_ratio, 1e-12));
    CHECK_THAT(actual.print_flow_ratio, WithinAbs(expected.print_flow_ratio, 1e-12));
    CHECK_THAT(actual.surface_flow_ratio, WithinAbs(expected.surface_flow_ratio, 1e-12));
    CHECK_THAT(actual.contact_to_tcp, WithinAbs(expected.contact_to_tcp, 1e-12));
    REQUIRE(actual.local_width_limits);
    CHECK_THAT(actual.local_width_limits->minimum_mm, WithinAbs(expected.local_width_limits->minimum_mm, 1e-12));
    CHECK_THAT(actual.local_width_limits->maximum_mm, WithinAbs(expected.local_width_limits->maximum_mm, 1e-12));
}

const FM::Region& material_region(const FM::Result& result, int id)
{
    const auto found = std::find_if(result.regions.begin(), result.regions.end(),
                                    [id](const FM::Region& region) { return region.region == id; });
    REQUIRE(found != result.regions.end());
    return *found;
}

void check_paths(const FH::Result& result)
{
    REQUIRE(result.preview.timeline.valid());
    std::array<double, 3> timeline_supply{}, cell_supply{};
    std::array<std::set<int>, 3> layers;
    std::array<int, 3> outer_layers{};
    std::map<int, std::vector<const PV::Motion*>> extrusion_by_layer;
    for (int head = 0; head < 3; ++head) {
        const auto& track = result.preview.timeline.heads[head];
        REQUIRE_FALSE(track.empty());
        for (const auto& motion : track) {
            CHECK(motion.head == head);
            CHECK(motion.region == head + 1);
            REQUIRE(motion.process);
            check_profile(*motion.process, head, true);
            REQUIRE(motion.end > motion.start);
            if (!motion.extrusion) {
                CHECK(motion.kind == PV::MotionKind::Travel);
                CHECK_THAT(motion.mm3_per_mm, WithinAbs(0., 0.));
                continue;
            }
            CHECK(motion.kind == PV::MotionKind::Extrude);
            const auto& p              = *motion.process;
            const double section       = p.layer_height * (p.line_width - p.layer_height * (1. - std::acos(-1.) / 4.));
            const double supplied_area = section * p.flow_ratio * p.print_flow_ratio * p.surface_flow_ratio;
            const double length        = (motion.to.position - motion.from.position).norm();
            REQUIRE(length > 0.);
            CHECK_THAT(motion.mm3_per_mm, WithinAbs(supplied_area, 1e-12));
            CHECK_THAT(length / (motion.end - motion.start), WithinAbs(std::min(p.speed, p.volumetric_limit / supplied_area), 1e-8));
            CHECK_THAT(length * motion.mm3_per_mm / (motion.end - motion.start), WithinAbs(p.volumetric_limit, 1e-8));
            CHECK_THAT((motion.from.axis - Vec3d::UnitZ()).norm(), WithinAbs(0., 1e-12));
            CHECK_THAT((motion.to.axis - Vec3d::UnitZ()).norm(), WithinAbs(0., 1e-12));
            timeline_supply[head] += length * motion.mm3_per_mm;
            extrusion_by_layer[motion.layer].push_back(&motion);
        }
    }
    for (const auto& run : result.material_runs) {
        REQUIRE(run.head >= 0);
        REQUIRE(run.head < 3);
        const int head = run.head;
        CHECK(run.region == head + 1);
        CHECK(run.material == profile(head).material_id);
        CHECK_FALSE(run.closed);
        REQUIRE(layers[head].insert(run.layer).second);
        outer_layers[head] += run.outer_layer;
        const auto found = extrusion_by_layer.find(run.layer);
        REQUIRE(found != extrusion_by_layer.end());
        REQUIRE(run.stations.size() == found->second.size() + 1);
        REQUIRE(run.supplied_volumes.size() == found->second.size());
        double layer_supply = 0.;
        for (std::size_t i = 0; i < run.supplied_volumes.size(); ++i) {
            const auto& motion      = *found->second[i];
            const auto& a           = run.stations[i];
            const auto& b           = run.stations[i + 1];
            const double tcp_offset = heights[head] / 2. + profile(head).contact_to_tcp;
            CHECK_THAT((motion.from.position - a.center - tcp_offset * a.axis).norm(), WithinAbs(0., 1e-10));
            CHECK_THAT((motion.to.position - b.center - tcp_offset * b.axis).norm(), WithinAbs(0., 1e-10));
            CHECK_THAT(a.width, WithinAbs(4. / row_counts[head], 1e-12));
            CHECK_THAT(a.height, WithinAbs(heights[head], 1e-12));
            REQUIRE(a.height_endpoints);
            CHECK((*a.height_endpoints)[0].z() >= top_z - depth(head) - 1e-12);
            CHECK((*a.height_endpoints)[1].z() <= top_z + 1e-12);
            CHECK_THAT(run.supplied_volumes[i], WithinAbs((motion.to.position - motion.from.position).norm() * motion.mm3_per_mm, 1e-10));
            layer_supply += run.supplied_volumes[i];
        }
        CHECK_THAT(layer_supply, WithinAbs(region_area * heights[head], 1e-9));
        cell_supply[head] += layer_supply;
    }
    for (int head = 0; head < 3; ++head) {
        CHECK(layers[head].size() == std::size_t(layer_counts[head]));
        CHECK(outer_layers[head] == 1);
        CHECK_THAT(timeline_supply[head], WithinAbs(wanted_volume(head), 1e-9));
        CHECK_THAT(cell_supply[head], WithinAbs(timeline_supply[head], 1e-9));
    }
}
} // namespace

TEST_CASE("Three independent nozzle and material profiles fill one adjacent multilayer workpiece in every region order",
          "[FormorixMultiMaterial]")
{
    const auto canonical = workpiece();
    REQUIRE(FG::valid_solid(canonical.solid));
    std::array<int, 3> order{{0, 1, 2}};
    do {
        CAPTURE(order[0], order[1], order[2]);
        auto input = canonical;
        for (int i = 0; i < 3; ++i)
            input.regions[i] = canonical.regions[order[i]];
        const auto result = FH::generate(input);
        INFO(result.error);
        INFO(result.material.error);
        std::cout << std::setprecision(17) << "MULTI_MATERIAL,order=" << order[0] << order[1] << order[2] << ",hybrid_ok=" << result.ok()
                  << ",material_status=" << int(result.material.status) << ",elapsed_s=" << result.material.elapsed_seconds
                  << ",missing=" << result.material.missing_volume << ",outside=" << result.material.outside_volume
                  << ",duplicate=" << result.material.duplicate_volume << ",core_overlap=" << result.material.core_overlap_volume
                  << ",error=" << result.material.error << "\n";
        REQUIRE(result.ok());
        REQUIRE(result.material.ok());
        REQUIRE(result.regions.size() == 3);
        REQUIRE(result.material.regions.size() == 3);
        CHECK(input.solid.vertices == canonical.solid.vertices);
        CHECK(input.solid.faces == canonical.solid.faces);
        CHECK(result.original.vertices == canonical.solid.vertices);
        CHECK(result.original.faces == canonical.solid.faces);
        CHECK(result.partition.output_topology_preserved);
        CHECK(FG::valid_solid(result.core));
        CHECK_THAT(result.partition.input_volume, WithinAbs(360., 1e-10));
        CHECK_THAT(FG::volume(result.core), WithinAbs(321., 1e-9));
        CHECK_THAT(result.partition.volume_residual, WithinAbs(0., 1e-9));
        CHECK_THAT(result.material.union_volume, WithinAbs(39., 1e-9));
        CHECK_THAT(result.material.supplied_volume, WithinAbs(39., 1e-9));
        CHECK_THAT(result.material.missing_volume, WithinAbs(0., 0.));
        CHECK_THAT(result.material.outside_volume, WithinAbs(0., 0.));
        CHECK_THAT(result.material.duplicate_volume, WithinAbs(0., 0.));
        CHECK_THAT(result.material.core_overlap_volume, WithinAbs(0., 0.));
        CHECK(result.material.material_overlaps.empty());
        REQUIRE(result.material.boundary_distance.bounded);
        CHECK(result.material.boundary_distance.upper_bound < .01);
        for (const auto& region : result.regions) {
            const int head = region.id - 1;
            REQUIRE(head >= 0);
            REQUIRE(head < 3);
            CHECK(region.head == head);
            check_profile(region.process, head, false);
            CHECK(region.adaptive_boundary);
            CHECK_THAT(region.minimum_local_width, WithinAbs(local_width(head), 1e-12));
            CHECK_THAT(region.maximum_local_width, WithinAbs(local_width(head), 1e-12));
            CHECK_THAT(region.shell_depth, WithinAbs(depth(head), 1e-12));
            CHECK_THAT(region.shell_volume, WithinAbs(wanted_volume(head), 1e-9));
            CHECK(region.accuracy.whole_surface_covered);
            CHECK(region.accuracy.material_partition_verified);
            CHECK(region.accuracy.material_volume_plausible);
            CHECK(region.accuracy.verified(.01));
            CHECK_FALSE(region.underlay.coverage_bounded);
            const auto& material = material_region(result.material, region.id);
            CHECK(material.material == profile(head).material_id);
            CHECK(material.coverage == FM::Coverage::Full);
            CHECK_THAT(material.supplied_volume, WithinAbs(wanted_volume(head), 1e-9));
            CHECK_THAT(material.target_volume, WithinAbs(wanted_volume(head), 1e-9));
            CHECK_THAT(material.supply_geometry_residual, WithinAbs(0., 1e-9));
            CHECK_THAT(material.missing_volume, WithinAbs(0., 0.));
            CHECK_THAT(material.outside_volume, WithinAbs(0., 0.));
            CHECK(material.nominal_outer_distance.bounded);
            CHECK(material.nominal_outer_distance.upper_bound < .01);
            std::cout << "MULTI_MATERIAL_REGION,id=" << region.id << ",nozzle=" << region.process.nozzle_diameter
                      << ",height=" << heights[head] << ",depth=" << depth(head) << ",local_width=" << region.minimum_local_width
                      << ",supply=" << material.supplied_volume << ",residual=" << material.supply_geometry_residual
                      << ",maximum=" << material.nominal_outer_distance.maximum << ",mean=" << material.nominal_outer_distance.mean
                      << ",rms=" << material.nominal_outer_distance.rms << ",upper=" << material.nominal_outer_distance.upper_bound
                      << ",bounds=" << region.accuracy.bounds[0] << '/' << region.accuracy.bounds[1] << '/' << region.accuracy.bounds[2]
                      << '/' << region.accuracy.bounds[3] << '/' << region.accuracy.bounds[4]
                      << ",verified=" << region.accuracy.verified(.01) << ",adhesion=unverified\n";
        }
        std::size_t material_interfaces = 0;
        for (const auto& seam : result.partition.interfaces) {
            if (seam.first == 0 || seam.second == 0)
                continue;
            ++material_interfaces;
            const int low = std::min(seam.first, seam.second), high = std::max(seam.first, seam.second);
            CHECK(high == low + 1);
            REQUIRE_FALSE(seam.mesh.faces.empty());
            double area = 0.;
            for (const auto& p : seam.mesh.vertices) {
                CHECK_THAT(p.x(), WithinAbs(6. * low, 1e-10));
                CHECK(p.z() >= top_z - std::min(depth(low - 1), depth(high - 1)) - 1e-12);
                CHECK(p.z() <= top_z + 1e-12);
            }
            for (const auto& f : seam.mesh.faces)
                area +=
                    (seam.mesh.vertices[f[1]] - seam.mesh.vertices[f[0]]).cross(seam.mesh.vertices[f[2]] - seam.mesh.vertices[f[0]]).norm() /
                    2.;
            CHECK_THAT(area, WithinAbs(4. * std::min(depth(low - 1), depth(high - 1)), 1e-10));
            CHECK_FALSE(PS::nominal_boundary_contact(profile(low - 1), profile(high - 1)).adhesion_verified);
        }
        CHECK(material_interfaces == 2);
        check_paths(result);
        double previous_end = 0.;
        for (const auto& region : input.regions) {
            const auto& track = result.preview.timeline.heads[region.head];
            REQUIRE_FALSE(track.empty());
            CHECK(track.front().start >= previous_end);
            previous_end = track.back().end;
            check_profile(region.process, region.head, false); // the planner must not overwrite the input profile
        }
        CHECK_THAT(result.preview.timeline.duration(), WithinAbs(previous_end, 1e-10));
    } while (std::next_permutation(order.begin(), order.end()));

    PS::UnderlayEvidence contact;
    contact.gap_upper_bound = contact.allowed_gap = 0.;
    contact.coverage_bounded = contact.finished = true;
    CHECK(contact.compatibility == PS::Compatibility::Unknown);
    CHECK_FALSE(contact.nominally_supported()); // geometry does not establish material adhesion
}

TEST_CASE("An absent inner layer remains a regional material deficit despite two complete neighboring materials", "[FormorixMultiMaterial]")
{
    const auto input     = workpiece();
    const auto generated = FH::generate(input);
    INFO(generated.error);
    INFO(generated.material.error);
    REQUIRE(generated.ok());
    REQUIRE(generated.material.ok());
    auto runs          = generated.material_runs;
    const auto removed = std::find_if(runs.begin(), runs.end(), [](const FM::SweepRun& run) { return run.head == 1 && !run.outer_layer; });
    REQUIRE(removed != runs.end());
    runs.erase(removed);
    std::vector<FM::Target> targets;
    for (const auto& part : generated.partition.parts)
        if (part.region > 0)
            targets.push_back({part.region, profile(part.region - 1).material_id, part.mesh, input.regions[part.region - 1].surface});
    const auto audit = FM::audit(targets, generated.core, runs, input.material_options);
    INFO(audit.error);
    REQUIRE(audit.ok());
    CHECK_THAT(audit.missing_volume, WithinAbs(region_area * heights[1], 1e-9));
    CHECK_THAT(audit.supplied_volume, WithinAbs(39. - region_area * heights[1], 1e-9));
    CHECK_THAT(audit.outside_volume, WithinAbs(0., 0.));
    CHECK_THAT(audit.core_overlap_volume, WithinAbs(0., 0.));
    CHECK(audit.material_overlaps.empty());
    for (int head = 0; head < 3; ++head) {
        const auto& region = material_region(audit, head + 1);
        CHECK(region.coverage == (head == 1 ? FM::Coverage::Partial : FM::Coverage::Full));
        CHECK_THAT(region.missing_volume, WithinAbs(head == 1 ? region_area * heights[head] : 0., 1e-9));
        CHECK(region.nominal_outer_distance.bounded);
        CHECK(region.nominal_outer_distance.upper_bound < .01);
    }
    CHECK(audit.boundary_distance.lower_bound > .01);
    std::cout << "MULTI_MATERIAL_MISSING_INNER,missing=" << audit.missing_volume
              << ",boundary_lower=" << audit.boundary_distance.lower_bound << ",boundary_upper=" << audit.boundary_distance.upper_bound
              << ",outer_B_upper=" << material_region(audit, 2).nominal_outer_distance.upper_bound << "\n";
}
