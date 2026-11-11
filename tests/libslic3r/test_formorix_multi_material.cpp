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
#include "libslic3r/FormorixProfiles.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Format/ProjectStrategy.hpp"
#include "libslic3r/Point.hpp"
#include "test_utils.hpp"
#include <boost/filesystem.hpp>
#include <boost/filesystem/operations.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
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
namespace FR = FormorixProfiles;
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

void native_sources(PresetBundle& bundle, FS::Project& project)
{
    const auto base_printer  = bundle.printers.get_edited_preset();
    const auto base_material = bundle.filaments.get_edited_preset();
    const auto base_process  = bundle.prints.get_edited_preset();
    const std::array<const char*, 3> materials{{"PETG", "PVA", "PETG-CF"}};
    for (int h = 0; h < 3; ++h) {
        const auto p            = profile(h);
        auto& head              = project.heads[h];
        const std::string label = "Formorix reference head " + std::to_string(h + 1);
        head.assign_nozzle(label + " nozzle", 0);
        head.assign_material(label + " " + materials[h]);
        head.assign_process(label + " process");
        head.assign_width_limits(p.local_width_limits);
        head.tasks = FS::Surface | (h == 0 ? FS::Build : h == 1 ? FS::Support : FS::Reinforcement);

        // Actual native parent references are necessary: Orca deliberately
        // rejects orphaned embedded printer/process presets on project import.
        auto printer = base_printer.config;
        printer.set_key_value("inherits", new ConfigOptionString(base_printer.name));
        printer.set_key_value("printer_settings_id", new ConfigOptionString(head.printer_preset));
        printer.set_key_value("printer_model", new ConfigOptionString("Formorix 5D"));
        printer.set_key_value("nozzle_diameter", new ConfigOptionFloats{p.nozzle_diameter});
        printer.set_key_value("printable_area", new ConfigOptionPoints{{0., 0.}, {1600., 0.}, {1600., 350.}, {0., 350.}});
        printer.set_key_value("printable_height", new ConfigOptionFloat(350.));
        printer.set_key_value("formorix_5d_enabled", new ConfigOptionBool(true));
        printer.set_key_value("formorix_5d_max_tilt", new ConfigOptionFloat(75.));
        bundle.printers.load_preset("", head.printer_preset, printer, false);

        auto material = base_material.config;
        material.set_key_value("inherits", new ConfigOptionString(base_material.name));
        material.set_key_value("filament_settings_id", new ConfigOptionStrings{head.material_preset});
        material.set_key_value("filament_type", new ConfigOptionStrings{materials[h]});
        material.set_key_value("filament_colour", new ConfigOptionStrings{std::array<const char*, 3>{{"#4A90D9", "#F6D55C", "#D95F59"}}[h]});
        material.set_key_value("nozzle_temperature", new ConfigOptionInts{int(p.temperature)});
        material.set_key_value("nozzle_temperature_initial_layer", new ConfigOptionInts{int(p.temperature)});
        material.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats{p.volumetric_limit});
        material.set_key_value("filament_flow_ratio", new ConfigOptionFloats{p.flow_ratio});
        material.set_key_value("filament_shrink", new ConfigOptionPercents{100.});
        material.set_key_value("filament_shrinkage_compensation_z", new ConfigOptionPercents{100.});
        bundle.filaments.load_preset("", head.material_preset, material, false).filament_id = p.material_id;

        auto process = base_process.config;
        process.set_key_value("inherits", new ConfigOptionString(base_process.name));
        process.set_key_value("print_settings_id", new ConfigOptionString(head.process_preset));
        process.set_key_value("outer_wall_line_width", new ConfigOptionFloatOrPercent(p.line_width, false));
        process.set_key_value("layer_height", new ConfigOptionFloat(p.layer_height));
        process.set_key_value("outer_wall_speed", new ConfigOptionFloatsNullable{p.speed});
        process.set_key_value("print_flow_ratio", new ConfigOptionFloat(p.print_flow_ratio));
        process.set_key_value("set_other_flow_ratios", new ConfigOptionBool(true));
        process.set_key_value("outer_wall_flow_ratio", new ConfigOptionFloat(p.surface_flow_ratio));
        bundle.prints.load_preset("", head.process_preset, process, false);

        // A persisted snapshot is informational only. Deliberately obsolete
        // values make accidental reuse distinguishable from fresh resolution.
        head.process                  = p;
        head.process->temperature     = 123.;
        head.process->material_id     = "obsolete cached material";
        head.process->nozzle_diameter = .3;
    }
    REQUIRE(bundle.printers.select_preset_by_name(project.heads[0].printer_preset, true));
    REQUIRE(bundle.prints.select_preset_by_name(project.heads[0].process_preset, true));
    REQUIRE(bundle.filaments.select_preset_by_name(project.heads[0].material_preset, true));
    bundle.filament_presets = {project.heads[0].material_preset};
}

std::array<PS::Profile, 3> resolve_native(PresetBundle& bundle, FS::Project& project)
{
    std::array<std::string, 3> signatures;
    REQUIRE(FR::invalidate_changed(project, bundle, signatures));
    std::array<PS::Profile, 3> result;
    for (int h = 0; h < 3; ++h) {
        REQUIRE_FALSE(project.heads[h].process);
        result[h] = FR::refresh(bundle, project.heads[h]).process;
        CHECK_THAT(result[h].temperature, WithinAbs(profile(h).temperature, 1e-12));
        CHECK_THAT(result[h].nozzle_diameter, WithinAbs(profile(h).nozzle_diameter, 1e-12));
        CHECK_THAT(result[h].contact_to_tcp, WithinAbs(0., 0.)); // actual Orca source value, not the saved 2/5/9 mm snapshot
        CHECK(result[h].material_id != "obsolete cached material");
        CHECK(result[h].material_id == profile(h).material_id);
    }
    return result;
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

TEST_CASE("Native projects preserve three painted material regions while recomputing from imported Orca sources",
          "[FormorixMultiMaterial][NativeRoundTrip]")
{
    ScopedSlic3rTemporaryDir temporary("formorix_three_materials");
    const auto reference = workpiece();
    Model model;
    model.set_backup_path((temporary.path() / "source-backup").string());
    auto* object = model.add_object();
    object->name = "Three independent materials - nominal hybrid reference";
    auto* volume = object->add_volume(TriangleMesh(FH::to_float(reference.solid).mesh), ModelVolumeType::MODEL_PART, false);
    volume->name = "Original 18 x 4 x 5 mm body";
    object->add_instance()->set_offset(Vec3d(60., 40., 0.));
    TriangleSelector paint(volume->mesh());
    FS::Project project;
    project.strategy = FS::Strategy::MaterialRoles;
    for (int h = 0; h < 3; ++h) {
        // workpiece() emits the two top triangles first in each eight-face strip.
        paint.set_facet(8 * h, EnforcerBlockerType(h + 1));
        paint.set_facet(8 * h + 1, EnforcerBlockerType(h + 1));
        FS::Region region;
        region.id       = h + 1;
        region.name     = "Surface " + std::string(1, char('A' + h));
        region.settings = reference.regions[h].settings;
        project.regions.push_back(region);
    }
    volume->formorix_facets.set(paint);
    volume->formorix_topology = FS::topology_id(volume->mesh().its);
    PresetBundle sources;
    native_sources(sources, project);
    model.formorix_surfaces      = project.encode();
    const auto original_vertices = volume->mesh().its.vertices;
    const auto original_faces    = volume->mesh().its.indices;

    auto before_project        = project;
    const auto before_profiles = resolve_native(sources, before_project);
    const auto before_input    = FH::extract(*object, 0, before_project, before_profiles);
    const auto before          = FH::generate(before_input);
    INFO(before.error);
    INFO(before.material.error);
    REQUIRE(before.ok());
    REQUIRE(before.material.ok());
    REQUIRE(before.regions.size() == 3);

    std::vector<std::unique_ptr<Preset>> embedded;
    FR::append_project_sources(sources, project, embedded);
    REQUIRE(embedded.size() == 9);
    FR::append_project_sources(sources, project, embedded);
    CHECK(embedded.size() == 9); // repeated saving does not duplicate inactive sources
    for (const auto& head : project.heads) {
        CHECK_FALSE(sources.printers.find_preset(head.printer_preset, false)->is_project_embedded);
        CHECK_FALSE(sources.filaments.find_preset(head.material_preset, false)->is_project_embedded);
        CHECK_FALSE(sources.prints.find_preset(head.process_preset, false)->is_project_embedded);
    }
    auto config = sources.full_config();
    PlateData plate;
    plate.plate_index = 0;
    plate.objects_and_instances.emplace_back(0, 0);
    StoreParams store;
    store.path            = (temporary.path() / "formorix-three-materials.3mf").string();
    store.model           = &model;
    store.config          = &config;
    store.plate_data_list = {&plate};
    store.strategy        = SaveStrategy::Zip64 | SaveStrategy::Silence;
    for (const auto& preset : embedded)
        store.project_presets.push_back(preset.get());
    REQUIRE(store_bbs_3mf(store));
    CHECK(volume->mesh().its.vertices == original_vertices);
    CHECK(volume->mesh().its.indices == original_faces);

    Model restored;
    restored.set_backup_path((temporary.path() / "restored-backup").string());
    DynamicPrintConfig loaded_config;
    ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Disable);
    PlateDataPtrs plates;
    std::vector<Preset*> imported;
    bool is_bbl = false, is_orca = false;
    Semver file_version;
    const bool loaded = load_bbs_3mf(store.path.c_str(), &loaded_config, &substitutions, &restored, &plates, &imported, &is_bbl, &is_orca,
                                     &file_version, nullptr, LoadStrategy::LoadModel | LoadStrategy::LoadConfig);
    release_PlateData_list(plates);
    std::vector<std::unique_ptr<Preset>> imported_owners;
    for (auto* preset : imported)
        imported_owners.emplace_back(preset);
    REQUIRE(loaded);
    REQUIRE(imported.size() == 9);
    for (const auto* preset : imported)
        if (preset->type == Preset::TYPE_FILAMENT) {
            const auto head = std::find_if(project.heads.begin(), project.heads.end(),
                                           [&](const auto& entry) { return entry.material_preset == preset->name; });
            REQUIRE(head != project.heads.end());
            CHECK(preset->filament_id == profile(int(head - project.heads.begin())).material_id);
        }
    REQUIRE(restored.objects.size() == 1);
    REQUIRE(restored.objects[0]->volumes.size() == 1);
    REQUIRE(restored.objects[0]->instances.size() == 1);
    CHECK(restored.formorix_surfaces == model.formorix_surfaces);
    CHECK(restored.objects[0]->volumes[0]->formorix_facets.equals(volume->formorix_facets));
    CHECK(FS::painting_matches(*restored.objects[0]->volumes[0]));
    CHECK(restored.objects[0]->volumes[0]->mmu_segmentation_facets.empty());
    std::string error;
    auto restored_project = FS::Project::decode(restored.formorix_surfaces, error);
    INFO(error);
    REQUIRE(restored_project);
    REQUIRE(restored_project->regions.size() == 3);
    for (int h = 0; h < 3; ++h) {
        const auto& head = restored_project->heads[h];
        REQUIRE(head.process);
        CHECK_THAT(head.process->temperature, WithinAbs(123., 0.));
        CHECK(head.process->material_id == "obsolete cached material");
        CHECK(head.printer_preset == project.heads[h].printer_preset);
        CHECK(head.material_preset == project.heads[h].material_preset);
        CHECK(head.process_preset == project.heads[h].process_preset);
        CHECK(head.nozzle_index == project.heads[h].nozzle_index);
        CHECK(restored_project->regions[h].settings.head_mask == (1u << h));
    }

    // Follow the real GUI sequence, including active full-config import. No
    // test-only name mapping or saved Process snapshot may rescue a binding.
    PresetBundle loaded_sources;
    std::vector<std::unique_ptr<Preset>> archive_copies;
    std::vector<Preset*> archived_sources;
    for (const auto* preset : imported) {
        archive_copies.push_back(std::make_unique<Preset>(*preset));
        archived_sources.push_back(archive_copies.back().get());
    }
    REQUIRE_NOTHROW(FR::validate_project_sources(loaded_sources, *restored_project, archived_sources));
    REQUIRE_NOTHROW(FR::validate_project_sources(sources, *restored_project, archived_sources));
    loaded_sources.load_project_embedded_presets(imported, ForwardCompatibilitySubstitutionRule::Disable);
    loaded_sources.load_config_model(store.path, loaded_config, file_version);
    // This is also the real GUI save collector: inactive head sources must stay
    // embedded when the loaded project is saved again.
    std::vector<std::unique_ptr<Preset>> saved_again;
    for (auto* preset : loaded_sources.get_current_project_embedded_presets())
        saved_again.emplace_back(preset);
    REQUIRE(saved_again.size() == 9);
    for (const auto& head : restored_project->heads) {
        REQUIRE(loaded_sources.printers.find_preset(head.printer_preset, false));
        REQUIRE(loaded_sources.filaments.find_preset(head.material_preset, false));
        REQUIRE(loaded_sources.prints.find_preset(head.process_preset, false));
    }
    const auto after_profiles = resolve_native(loaded_sources, *restored_project);
    REQUIRE_NOTHROW(FR::validate_project_sources(loaded_sources, *restored_project, archived_sources));
    for (int h = 0; h < 3; ++h)
        CHECK(nlohmann::json(after_profiles[h]) == nlohmann::json(before_profiles[h]));
    const auto after_input = FH::extract(*restored.objects[0], 0, *restored_project, after_profiles);
    CHECK(after_input.solid.vertices == before_input.solid.vertices);
    CHECK(after_input.solid.faces == before_input.solid.faces);
    REQUIRE(after_input.regions.size() == before_input.regions.size());
    for (std::size_t i = 0; i < after_input.regions.size(); ++i) {
        CHECK(after_input.regions[i].id == before_input.regions[i].id);
        CHECK(after_input.regions[i].head == before_input.regions[i].head);
        CHECK(after_input.regions[i].surface.vertices == before_input.regions[i].surface.vertices);
        CHECK(after_input.regions[i].surface.faces == before_input.regions[i].surface.faces);
        CHECK_FALSE(after_input.regions[i].cad_reference);
    }
    const auto after = FH::generate(after_input);
    INFO(after.error);
    INFO(after.material.error);
    REQUIRE(after.ok());
    REQUIRE(after.material.ok());
    CHECK_THAT(FG::volume(after.core), WithinAbs(321., 1e-9));
    CHECK_THAT(FG::volume(after.core), WithinAbs(FG::volume(before.core), 1e-10));
    CHECK_THAT(after.material.union_volume, WithinAbs(39., 1e-9));
    CHECK_THAT(after.material.union_volume, WithinAbs(before.material.union_volume, 1e-10));
    CHECK_THAT(after.material.missing_volume, WithinAbs(0., 0.));
    CHECK_THAT(after.material.outside_volume, WithinAbs(0., 0.));
    CHECK_THAT(after.material.duplicate_volume, WithinAbs(0., 0.));
    CHECK_THAT(after.material.core_overlap_volume, WithinAbs(0., 0.));
    REQUIRE(after.regions.size() == 3);
    for (const auto& region : after.regions) {
        const auto& current  = material_region(after.material, region.id);
        const auto& previous = material_region(before.material, region.id);
        CHECK(current.material == previous.material);
        CHECK(current.coverage == FM::Coverage::Full);
        CHECK_THAT(current.supplied_volume, WithinAbs(wanted_volume(region.head), 1e-9));
        CHECK_THAT(current.supplied_volume, WithinAbs(previous.supplied_volume, 1e-10));
        CHECK(current.boundary_distance.bounded);
        CHECK(current.nominal_outer_distance.bounded);
        CHECK_THAT(current.boundary_distance.upper_bound, WithinAbs(previous.boundary_distance.upper_bound, 1e-12));
        CHECK_THAT(current.nominal_outer_distance.upper_bound, WithinAbs(previous.nominal_outer_distance.upper_bound, 1e-12));
        CHECK(region.accuracy.material_partition_verified);
        CHECK_FALSE(std::isfinite(region.accuracy.bounds[0]));
        CHECK_FALSE(region.accuracy.verified()); // native projects carry no analytic CAD reference
        std::cout << std::setprecision(17) << "MULTI_MATERIAL_3MF,region=" << region.id << ",nozzle=" << region.process.nozzle_diameter
                  << ",supply=" << current.supplied_volume << ",missing=" << current.missing_volume << ",outside=" << current.outside_volume
                  << ",spatial_upper=" << current.boundary_distance.upper_bound
                  << ",outer_upper=" << current.nominal_outer_distance.upper_bound << ",cad_verified=" << region.accuracy.verified()
                  << "\n";
    }
    CHECK(after.preview.timeline.valid());
    CHECK_THAT(after.preview.timeline.duration(), WithinAbs(before.preview.timeline.duration(), 1e-9));
    for (int h = 0; h < 3; ++h) {
        REQUIRE_FALSE(after.preview.timeline.heads[h].empty());
        CHECK(after.preview.timeline.heads[h].size() == before.preview.timeline.heads[h].size());
        for (const auto& motion : after.preview.timeline.heads[h]) {
            CHECK(motion.head == h);
            CHECK(motion.region == h + 1);
            REQUIRE(motion.process);
            CHECK(motion.process->material_id == after_profiles[h].material_id);
            CHECK_THAT(motion.process->nozzle_diameter, WithinAbs(after_profiles[h].nozzle_diameter, 1e-12));
        }
    }
    // Native import skips already existing same-name presets. A conflicting
    // inactive head source must therefore be rejected before that lossy import,
    // rather than silently calculating with its locally different parameters.
    auto* changed_nozzle = loaded_sources.printers.find_preset(restored_project->heads[1].printer_preset, false);
    REQUIRE(changed_nozzle);
    const auto saved_modes = *changed_nozzle->config.option<ConfigOptionFloats>("machine_max_junction_deviation");
    changed_nozzle->config.set_key_value("machine_max_junction_deviation", new ConfigOptionFloats{saved_modes.get_at(0), .02});
    CHECK_THROWS(FR::validate_project_sources(loaded_sources, *restored_project, archived_sources));
    CHECK(changed_nozzle->config.option<ConfigOptionFloats>("machine_max_junction_deviation")->get_at(1) == .02);
    changed_nozzle->config.set_key_value("machine_max_junction_deviation", saved_modes.clone());
    REQUIRE_NOTHROW(FR::validate_project_sources(loaded_sources, *restored_project, archived_sources));
    auto* conflicting = loaded_sources.filaments.find_preset(restored_project->heads[1].material_preset, false);
    REQUIRE(conflicting);
    conflicting->config.set_key_value("nozzle_temperature", new ConfigOptionInts{206});
    const auto before_rejection = FR::signature(FR::source(loaded_sources, restored_project->heads[1]));
    CHECK_THROWS(FR::validate_project_sources(loaded_sources, *restored_project, archived_sources));
    CHECK(FR::signature(FR::source(loaded_sources, restored_project->heads[1])) == before_rejection);
    CHECK(restored.formorix_surfaces == model.formorix_surfaces);
    // Opt-in developer artifact: the very archive validated above, with all
    // nine native source presets. Ordinary test runs leave no persistent file.
    if (const char* directory = std::getenv("FORMORIX_TEST_ARTIFACT_DIR"); directory && *directory) {
        const boost::filesystem::path destination(directory);
        boost::filesystem::create_directories(destination);
        const auto artifact = destination / "formorix-three-materials.3mf";
        boost::filesystem::copy_file(store.path, artifact, boost::filesystem::copy_option::overwrite_if_exists);
        std::cout << "FORMORIX_TEST_ARTIFACT " << artifact.string() << "\n";
    }
}

TEST_CASE("Portable head sources use edited copies without mutating originals or duplicating names",
          "[FormorixMultiMaterial][NativeRoundTrip]")
{
    PresetBundle bundle;
    FS::Project project;
    native_sources(bundle, project);
    auto& edited_material = bundle.filaments.get_edited_preset();
    edited_material.config.set_key_value("nozzle_temperature", new ConfigOptionInts{247});
    bundle.printers.get_edited_preset().config.set_key_value("printhost_apikey", new ConfigOptionString("synthetic-not-a-real-key"));
    const auto original_signature = FR::signature(FR::source(bundle, project.heads[0]));
    std::vector<std::unique_ptr<Preset>> copies;
    copies.push_back(std::make_unique<Preset>(*bundle.filaments.find_preset(project.heads[0].material_preset, false)));
    copies.front()->config.set_key_value("nozzle_temperature", new ConfigOptionInts{80});
    FR::append_project_sources(bundle, project, copies);
    REQUIRE(copies.size() == 9);
    CHECK(FR::signature(FR::source(bundle, project.heads[0])) == original_signature);
    const auto active = std::find_if(copies.begin(), copies.end(), [&](const auto& preset) {
        return preset->type == Preset::TYPE_FILAMENT && preset->name == project.heads[0].material_preset;
    });
    REQUIRE(active != copies.end());
    CHECK((*active)->config.option<ConfigOptionInts>("nozzle_temperature")->get_at(0) == 247);
    CHECK((*active)->filament_id == "A");
    for (const auto& copy : copies) {
        CHECK(copy->is_project_embedded);
        CHECK_FALSE(copy->is_default);
        CHECK_FALSE(copy->is_system);
        CHECK(copy->inherits() != copy->name);
        CHECK_FALSE(copy->config.has("printhost_apikey"));
    }
    std::vector<const Preset*> retained;
    for (const auto& copy : copies)
        retained.push_back(copy.get());
    project.heads[2].printer_preset = "Missing independent nozzle source";
    CHECK_THROWS(FR::append_project_sources(bundle, project, copies));
    REQUIRE(copies.size() == retained.size());
    for (std::size_t i = 0; i < retained.size(); ++i)
        CHECK(copies[i].get() == retained[i]);

    PresetBundle defaults;
    FS::Project default_project;
    std::vector<std::unique_ptr<Preset>> default_copies;
    FR::append_project_sources(defaults, default_project, default_copies);
    CHECK(default_copies.empty());
    default_project.heads[0].printer_preset = defaults.printers.get_edited_preset().name;
    defaults.printers.get_edited_preset().config.set_key_value("nozzle_diameter", new ConfigOptionFloats{.8});
    CHECK_THROWS(FR::append_project_sources(defaults, default_project, default_copies));
    CHECK(default_copies.empty());
}
