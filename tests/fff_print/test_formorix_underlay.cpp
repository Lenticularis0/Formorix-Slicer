#include <catch2/catch_test_macros.hpp>
#include "libslic3r/libslic3r.h"
#include <catch2/catch_message.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixCorePreview.hpp"
#include "libslic3r/FormorixGeometry.hpp"
#include "libslic3r/FormorixHybrid.hpp"
#include "libslic3r/FormorixProcess.hpp"
#include "libslic3r/FormorixSurfaces.hpp"
#include "libslic3r/FormorixUnderlay.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Format/ProjectStrategy.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "test_utils.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace Slic3r;
namespace CP = Slic3r::FormorixCorePreview;
namespace FG = Slic3r::FormorixGeometry;
namespace FH = Slic3r::FormorixHybrid;
namespace FS = Slic3r::FormorixSurfaces;
namespace FU = Slic3r::FormorixUnderlay;
using Catch::Matchers::WithinAbs;

namespace {
std::array<CP::Source, 3> sources(const DynamicPrintConfig& config)
{
    const double nozzle = config.opt_float("nozzle_diameter", 0);
    const double height = config.opt_float("layer_height");
    const auto* width = config.option<ConfigOptionFloatOrPercent>("outer_wall_line_width");
    REQUIRE(width);
    if (width->value == 0.) width = config.option<ConfigOptionFloatOrPercent>("line_width");
    REQUIRE(width);
    const auto native = Flow::new_from_config_width(frExternalPerimeter, *width, float(nozzle), float(height));
    const double resolved_width = !width->percent && width->value <= 0. ? double(native.width()) : width->get_abs_value(nozzle);
    std::array<CP::Source, 3> out;
    for (auto& source : out) {
        source.config = config;
        source.process = FormorixProcess::from_orca(config, 0, 0, "saved-material", "saved-nozzle", resolved_width,
                                                   height, config.opt_float("outer_wall_speed", 0));
    }
    return out;
}

DynamicPrintConfig sparse_config()
{
    auto config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({{"layer_height", "0.2"}, {"initial_layer_print_height", "0.2"},
                                   {"nozzle_diameter", "0.4"}, {"line_width", "0.45"},
                                   {"outer_wall_line_width", "0.45"}, {"inner_wall_line_width", "0.45"},
                                   {"sparse_infill_line_width", "0.45"}, {"internal_solid_infill_line_width", "0.45"},
                                   {"sparse_infill_density", "20%"}, {"sparse_infill_pattern", "rectilinear"},
                                   {"top_shell_layers", "0"}, {"bottom_shell_layers", "0"},
                                   {"top_shell_thickness", "0"}, {"bottom_shell_thickness", "0"},
                                   {"wall_loops", "1"}, {"enable_support", "0"}, {"brim_type", "no_brim"},
                                   {"skirt_loops", "0"}, {"elefant_foot_compensation", "0"},
                                   {"filament_flow_ratio", "0.98"}});
    return config;
}

FH::Result local_interface(const FormorixProcess::Profile& profile)
{
    FH::Result result;
    const auto solid = make_cube(20., 20., 4.);
    result.original.faces = solid.its.indices;
    for (const auto& vertex : solid.its.vertices) result.original.vertices.push_back(vertex.cast<double>());
    FH::RegionResult region;
    region.id = 1;
    region.process = profile;
    region.shell_depth = .4;
    region.surface.vertices = {{3., 3., 4.}, {10., 3., 4.}, {10., 17., 4.}, {3., 17., 4.}};
    region.surface.faces = {{0, 1, 2}, {0, 2, 3}};
    region.settings.layers = 2;
    region.settings.layer_height = .2;
    region.settings.use_infill = true;
    region.settings.dense_underlay = true;
    region.settings.underlay_density = 100.;
    result.partition = FG::partition(result.original, {{1, region.surface, std::vector<Vec3d>(4, Vec3d::UnitZ()),
                                                       std::vector<double>(4, .4)}});
    INFO(result.partition.error);
    REQUIRE(result.partition.ok());
    for (const auto& part : result.partition.parts) {
        if (part.region == 0) result.core = part.mesh;
        else region.shell_volume = part.volume;
    }
    REQUIRE_FALSE(result.core.faces.empty());
    result.regions.push_back(region);
    return result;
}

std::vector<FH::UnderlaySegment> segments(const CP::Result& native, bool raw)
{
    std::vector<FH::UnderlaySegment> out;
    for (const auto& path : native.paths) {
        if (path.nominal_supplied_mm3_per_mm <= 0.) continue;
        for (std::size_t i = 1; i < path.points.size(); ++i)
            out.push_back({path.points[i - 1], path.points[i], path.width, path.height, path.head, path.process.material_id,
                           path.support, path.role == erInternalInfill, is_bridge(path.role),
                           raw ? false : path.flow_geometry_unverified, path.dense_transition});
    }
    return out;
}

struct Amounts {
    double supplied = 0., geometric = 0., dense = 0.;
    std::size_t paths = 0, dense_paths = 0;
};
Amounts amounts(const CP::Result& native)
{
    Amounts out;
    for (const auto& path : native.paths) {
        ++out.paths;
        if (path.dense_transition) ++out.dense_paths;
        for (std::size_t i = 1; i < path.points.size(); ++i) {
            const double length = (path.points[i] - path.points[i - 1]).norm();
            out.supplied += length * path.nominal_supplied_mm3_per_mm;
            out.geometric += length * path.mm3_per_mm;
            if (path.dense_transition) out.dense += length * path.nominal_supplied_mm3_per_mm;
        }
    }
    return out;
}

// Compare actual finite segments on layers entirely below the local request.
// Sorting permits native entity ordering changes without hiding changed geometry.
std::vector<std::array<double, 9>> lower_segments(const CP::Result& native, double below)
{
    std::vector<std::array<double, 9>> out;
    for (const auto& path : native.paths)
        for (std::size_t i = 1; i < path.points.size(); ++i) {
            Vec3d a = path.points[i - 1], b = path.points[i];
            if (std::max(a.z(), b.z()) >= below - 1e-8) continue;
            if (std::array<double, 3>{b.x(), b.y(), b.z()} < std::array<double, 3>{a.x(), a.y(), a.z()}) std::swap(a, b);
            out.push_back({a.x(), a.y(), a.z(), b.x(), b.y(), b.z(), path.width, path.height, path.nominal_supplied_mm3_per_mm});
        }
    std::sort(out.begin(), out.end());
    return out;
}

void report(const char* name, const CP::Result& native, const FH::Result& hybrid)
{
    const auto volume = amounts(native);
    const auto& audit = hybrid.regions.front().underlay;
    std::cout << std::setprecision(15) << "UNDERLAY_NATIVE," << name << ",paths=" << volume.paths
              << ",dense_paths=" << volume.dense_paths << ",geometric_mm3=" << volume.geometric
              << ",supplied_mm3=" << volume.supplied << ",dense_supplied_mm3=" << volume.dense
              << ",modifiers=" << native.dense_modifier_volumes << ",samples=" << audit.samples
              << ",raw_missing=" << audit.raw_missing << ",strict_missing=" << audit.missing
              << ",unknown=" << audit.unknown_geometry << ",native_layer_gaps=" << audit.native_layer_gaps
              << ",modifier_float_bound_mm=" << native.modifier_float_conversion_bound
              << ",modifier_volume_error_mm3=" << native.modifier_volume_error
              << ",native_slice_expansion_bound_mm=" << native.native_slice_expansion_bound
              << ",modifier_boundary_retreat_bound_mm=" << native.modifier_boundary_retreat_bound
              << ",dense_requested_area_mm2=" << native.dense_requested_area
              << ",dense_applied_area_mm2=" << native.dense_applied_area << '\n';
}

// Diagnostic evidence only: bridge/uncalibrated sections stay unverified in the
// production audit and in the assertions below. More supplied volume alone is
// never substituted for improved contact against the actual shared interface.
void report_native_sections(const char* name, const CP::Result& native)
{
    struct Group {
        std::size_t paths = 0, segments = 0, bridges = 0, narrow = 0, nonplanar = 0;
        double length = 0., supplied = 0.;
    };
    using Key = std::tuple<int, bool, double, double, double>;
    std::map<Key, Group> groups;
    for (const auto& path : native.paths) {
        if (path.points.empty()) continue;
        auto& group = groups[{int(path.role), path.dense_transition, path.points.front().z(), path.width, path.height}];
        ++group.paths;
        for (std::size_t i = 1; i < path.points.size(); ++i) {
            ++group.segments;
            group.bridges += is_bridge(path.role);
            group.narrow += path.width < path.height;
            group.nonplanar += std::abs(path.points[i].z() - path.points[i - 1].z()) > 1e-8;
            const double length = (path.points[i] - path.points[i - 1]).norm();
            group.length += length;
            group.supplied += length * path.nominal_supplied_mm3_per_mm;
        }
    }
    for (const auto& entry : groups) {
        const auto& [role, dense, z, width, height] = entry.first;
        const auto& group = entry.second;
        std::cout << "UNDERLAY_NATIVE_SECTION," << name << ",role=" << role << ",dense=" << dense << ",z=" << z
                  << ",width=" << width << ",height=" << height << ",paths=" << group.paths << ",segments=" << group.segments
                  << ",bridge_segments=" << group.bridges << ",width_below_height=" << group.narrow
                  << ",nonplanar_segments=" << group.nonplanar << ",length_mm=" << group.length
                  << ",supplied_mm3=" << group.supplied << '\n';
    }
}

void report_interface_faces(const char* name, const FU::Preparation& prepared, const FH::Result& hybrid,
                            const FU::Analysis& analysis)
{
    struct Group {
        Vec3d normal = Vec3d::Zero();
        std::size_t probes = 0, raw_missing = 0, raw_shape_unknown = 0;
        std::array<std::size_t, 6> strict{};
        std::array<std::size_t, 4> raw{};
        double area = 0., raw_missing_area = 0., raw_shape_unknown_area = 0.;
    };
    for (std::size_t r = 0; r < prepared.regions.size(); ++r) {
        const auto& region = hybrid.regions[r];
        const double allowed = std::min(.05, region.process.layer_height * .25);
        REQUIRE(prepared.regions[r].probes.size() == region.underlay_contacts.size());
        REQUIRE(prepared.regions[r].probes.size() == analysis.regions[r].samples.size());
        std::map<std::pair<std::size_t, std::size_t>, Group> groups;
        for (std::size_t i = 0; i < prepared.regions[r].probes.size(); ++i) {
            const auto& probe = prepared.regions[r].probes[i];
            const auto& contact = region.underlay_contacts[i];
            auto& group = groups[{probe.source_interface, probe.source_face}];
            group.normal = probe.outward;
            ++group.probes;
            group.area += probe.area;
            ++group.strict[std::size_t(contact.kind)];
            ++group.raw[std::size_t(analysis.regions[r].samples[i].kind)];
            if (contact.raw_gap > allowed) {
                ++group.raw_missing;
                group.raw_missing_area += probe.area;
            }
            if (contact.raw_geometry_unknown) {
                ++group.raw_shape_unknown;
                group.raw_shape_unknown_area += probe.area;
            }
        }
        for (const auto& entry : groups) {
            const auto& group = entry.second;
            std::cout << "UNDERLAY_INTERFACE_FACE," << name << ",region=" << region.id << ",interface=" << entry.first.first
                      << ",face=" << entry.first.second << ",nx=" << group.normal.x() << ",ny=" << group.normal.y()
                      << ",nz=" << group.normal.z() << ",probes=" << group.probes << ",area_mm2=" << group.area
                      << ",raw_missing=" << group.raw_missing << ",raw_missing_area_mm2=" << group.raw_missing_area
                      << ",raw_shape_unknown=" << group.raw_shape_unknown
                      << ",raw_shape_unknown_area_mm2=" << group.raw_shape_unknown_area;
            for (std::size_t i = 0; i < group.strict.size(); ++i) std::cout << ",strict_kind_" << i << '=' << group.strict[i];
            for (std::size_t i = 0; i < group.raw.size(); ++i) std::cout << ",raw_kind_" << i << '=' << group.raw[i];
            std::cout << '\n';
        }
    }
}

double outside_supply(const CP::Result& native, double x_min)
{
    double out = 0.;
    for (const auto& path : native.paths)
        for (std::size_t i = 1; i < path.points.size(); ++i) {
            const Vec3d a = path.points[i - 1], b = path.points[i];
            double first = 0., last = 1.;
            if (a.x() < x_min && b.x() < x_min) continue;
            if (a.x() < x_min) first = (x_min - a.x()) / (b.x() - a.x());
            if (b.x() < x_min) last = (x_min - a.x()) / (b.x() - a.x());
            out += (last - first) * (b - a).norm() * path.nominal_supplied_mm3_per_mm;
        }
    return out;
}

void check_native_flow(const CP::Result& native, double filament_flow)
{
    for (const auto& path : native.paths) {
        CHECK_THAT(path.process.flow_ratio, WithinAbs(filament_flow, 0.));
        CHECK_THAT(path.nominal_supplied_mm3_per_mm,
                   WithinAbs(path.mm3_per_mm * filament_flow * path.process.print_flow_ratio * path.role_flow_ratio *
                                 path.first_layer_flow_ratio, 1e-12));
        if (path.nominal_supplied_mm3_per_mm > 0.) CHECK(path.flow_geometry_unverified);
    }
}
} // namespace

TEST_CASE("Native full density creates an unverified bridge instead of certifying better underlay", "[FormorixUnderlay]")
{
    const auto config = sparse_config();
    const auto configs = sources(config);
    auto before = local_interface(configs[0].process);
    const auto original = before.original;
    const auto prepared = FU::prepare(before);
    INFO(prepared.error);
    REQUIRE(prepared.ok());
    REQUIRE(prepared.regions.size() == 1);
    REQUIRE_FALSE(prepared.regions[0].probes.empty());
    const auto baseline = CP::slice(before.core, configs, 0, -1);
    INFO(baseline.error);
    REQUIRE(baseline.ok());
    FH::audit_underlay(before, segments(baseline, false));
    const auto analysis = FU::analyze(before, prepared, baseline);
    INFO(analysis.error);
    REQUIRE(analysis.ok());
    REQUIRE_FALSE(analysis.dense_requests.empty());
    REQUIRE(before.regions[0].underlay.raw_missing > 0);
    double bottom = std::numeric_limits<double>::infinity();
    for (const auto& request : analysis.dense_requests) {
        CHECK(request.region == 1);
        CHECK_THAT(request.density, WithinAbs(100., 0.));
        CHECK(request.bottom_z < request.slice_z);
        CHECK(request.slice_z < request.top_z);
        bottom = std::min(bottom, request.bottom_z);
    }
    const auto native = CP::slice(before.core, configs, 0, -1, {}, analysis.dense_requests);
    INFO(native.error);
    REQUIRE(native.ok());
    REQUIRE(native.dense_modifier_volumes > 0);
    auto after = before;
    FH::audit_underlay(after, segments(native, false));
    const auto revised = FU::analyze(after, prepared, native);
    INFO(revised.error);
    REQUIRE(revised.ok());
    std::cout << std::setprecision(15);
    report_native_sections("local_sparse", baseline);
    report_native_sections("local_dense_100", native);
    report_interface_faces("local_sparse", prepared, before, analysis);
    report_interface_faces("local_dense_100", prepared, after, revised);
    for (const auto& request : analysis.dense_requests) {
        double area = 0.;
        for (const auto& polygon : request.area) area += polygon.area() * SCALING_FACTOR * SCALING_FACTOR;
        std::cout << "UNDERLAY_REQUEST,region=" << request.region << ",bottom_z=" << request.bottom_z
                  << ",top_z=" << request.top_z << ",slice_z=" << request.slice_z << ",density=" << request.density
                  << ",components=" << request.area.size() << ",area_mm2=" << area << '\n';
    }
    CHECK(after.regions[0].interface_samples == before.regions[0].interface_samples);
    // Native 100% infill over sparse lower layers becomes a suspended circular
    // bridge. It cannot inherit the supported stadium contact model. Preserve
    // this measured negative case instead of treating more material as support.
    std::size_t dense_bridge_segments = 0, horizontal_samples = 0, horizontal_missing_before = 0;
    double horizontal_unknown_area = 0.;
    for (const auto& path : native.paths) {
        if (!path.dense_transition || path.role != erInternalBridgeInfill) continue;
        dense_bridge_segments += path.points.size() - 1;
        CHECK_THAT(path.width, WithinAbs(path.height, 1e-8));
    }
    REQUIRE(dense_bridge_segments > 0);
    CHECK(std::none_of(baseline.paths.begin(), baseline.paths.end(), [](const auto& path) { return is_bridge(path.role); }));
    const double allowed = std::min(.05, before.regions[0].process.layer_height * .25);
    for (std::size_t i = 0; i < prepared.regions[0].probes.size(); ++i) {
        const auto& probe = prepared.regions[0].probes[i];
        const auto& old_contact = before.regions[0].underlay_contacts[i];
        const auto& new_contact = after.regions[0].underlay_contacts[i];
        if (probe.outward.z() > .999999) {
            ++horizontal_samples;
            horizontal_missing_before += old_contact.raw_gap > allowed;
            horizontal_unknown_area += probe.area;
            CHECK_FALSE(old_contact.raw_geometry_unknown);
            CHECK(new_contact.raw_geometry_unknown);
            CHECK(new_contact.kind == FH::UnderlayKind::UnknownGeometry);
            CHECK(revised.regions[0].samples[i].kind == FU::RawKind::Unknown);
            CHECK(new_contact.raw_gap > allowed);
        } else {
            CHECK((new_contact.raw_gap > allowed) == (old_contact.raw_gap > allowed));
        }
    }
    REQUIRE(horizontal_samples > horizontal_missing_before);
    CHECK_THAT(horizontal_unknown_area, WithinAbs((10. - 3.) * (17. - 3.), 1e-10));
    CHECK(after.regions[0].underlay.raw_missing ==
          before.regions[0].underlay.raw_missing + horizontal_samples - horizontal_missing_before);
    CHECK(revised.regions[0].unknown_area >= horizontal_unknown_area);
    CHECK(after.regions[0].underlay.unknown_geometry > 0);
    CHECK_FALSE(after.regions[0].underlay.coverage_bounded);
    CHECK_FALSE(revised.regions[0].coverage_bounded);
    const auto old_amounts = amounts(baseline), new_amounts = amounts(native);
    CHECK(new_amounts.supplied > old_amounts.supplied);
    CHECK(new_amounts.dense_paths > 0);
    const auto lower_before = lower_segments(baseline, bottom), lower_after = lower_segments(native, bottom);
    REQUIRE_FALSE(lower_before.empty());
    CHECK(lower_before == lower_after);
    CHECK(config.diff(configs[0].config).empty());
    CHECK(before.original.vertices == original.vertices);
    CHECK(before.original.faces == original.faces);
    check_native_flow(baseline, .98);
    check_native_flow(native, .98);
    report("local_sparse", baseline, before);
    report("local_dense_100", native, after);
    std::cout << "UNDERLAY_OUTSIDE,x_min=14,before_mm3=" << outside_supply(baseline, 14.)
              << ",after_mm3=" << outside_supply(native, 14.) << ",unchanged_lower_segments=" << lower_before.size() << '\n';
}

TEST_CASE("Explicit local eighty percent infill improves sampled contact with unchanged material flow", "[FormorixUnderlay]")
{
    const auto config = sparse_config();
    const auto configs = sources(config);
    auto before = local_interface(configs[0].process);
    // A separate user-selected strategy, never an automatic replacement for
    // the 100% request whose native bridge remains unverified above.
    before.regions[0].settings.underlay_density = 80.;
    const auto prepared = FU::prepare(before);
    INFO(prepared.error);
    REQUIRE(prepared.ok());
    const auto baseline = CP::slice(before.core, configs, 0, -1);
    INFO(baseline.error);
    REQUIRE(baseline.ok());
    FH::audit_underlay(before, segments(baseline, false));
    const auto requested = FU::analyze(before, prepared, baseline);
    INFO(requested.error);
    REQUIRE(requested.ok());
    REQUIRE_FALSE(requested.dense_requests.empty());
    double bottom = std::numeric_limits<double>::infinity();
    for (const auto& request : requested.dense_requests) {
        CHECK_THAT(request.density, WithinAbs(80., 0.));
        bottom = std::min(bottom, request.bottom_z);
    }
    const auto native = CP::slice(before.core, configs, 0, -1, {}, requested.dense_requests);
    INFO(native.error);
    REQUIRE(native.ok());
    REQUIRE(native.dense_modifier_volumes > 0);
    auto after = before;
    FH::audit_underlay(after, segments(native, false));
    const auto revised = FU::analyze(after, prepared, native);
    INFO(revised.error);
    REQUIRE(revised.ok());
    CHECK(after.regions[0].interface_samples == before.regions[0].interface_samples);
    CHECK(after.regions[0].underlay.raw_missing < before.regions[0].underlay.raw_missing);
    CHECK(revised.regions[0].raw_missing_area < requested.regions[0].raw_missing_area);
    CHECK_THAT(revised.regions[0].unknown_area, WithinAbs(0., 0.));
    CHECK(after.regions[0].underlay.unknown_geometry > 0); // unchanged 0.98 supply is still uncalibrated
    CHECK_FALSE(after.regions[0].underlay.coverage_bounded);
    CHECK_FALSE(revised.regions[0].coverage_bounded);
    std::size_t actual_dense_infill = 0;
    for (const auto& path : native.paths) {
        CHECK_FALSE(is_bridge(path.role));
        if (path.dense_transition && path.role == erInternalInfill) {
            ++actual_dense_infill;
            CHECK_THAT(path.native_infill_density, WithinAbs(80., 0.));
        }
    }
    CHECK(actual_dense_infill > 0);
    CHECK(amounts(native).supplied > amounts(baseline).supplied);
    const auto lower_before = lower_segments(baseline, bottom), lower_after = lower_segments(native, bottom);
    REQUIRE_FALSE(lower_before.empty());
    CHECK(lower_before == lower_after);
    CHECK(config.diff(configs[0].config).empty());
    check_native_flow(baseline, .98);
    check_native_flow(native, .98);
    report("local_sparse_for_80", baseline, before);
    report("local_dense_80", native, after);
    std::cout << "UNDERLAY_LOCAL_80_AREAS,raw_missing_before_mm2=" << requested.regions[0].raw_missing_area
              << ",raw_missing_after_mm2=" << revised.regions[0].raw_missing_area
              << ",unknown_before_mm2=" << requested.regions[0].unknown_area
              << ",unknown_after_mm2=" << revised.regions[0].unknown_area
              << ",unchanged_lower_segments=" << lower_before.size() << '\n';
}

TEST_CASE("Disabled and already satisfied dense underlay leave native output unchanged", "[FormorixUnderlay]")
{
    const auto configs = sources(sparse_config());
    auto hybrid = local_interface(configs[0].process);
    const auto prepared = FU::prepare(hybrid);
    INFO(prepared.error);
    REQUIRE(prepared.ok());
    const auto baseline = CP::slice(hybrid.core, configs, 0, -1);
    INFO(baseline.error);
    REQUIRE(baseline.ok());
    const auto expected = lower_segments(baseline, std::numeric_limits<double>::infinity());
    for (bool enabled : {false, true}) {
        hybrid.regions[0].settings.dense_underlay = enabled;
        hybrid.regions[0].settings.underlay_density = 20.; // same as the native process, never an implicit increase
        FH::audit_underlay(hybrid, segments(baseline, false));
        const auto analysis = FU::analyze(hybrid, prepared, baseline);
        INFO(analysis.error);
        REQUIRE(analysis.ok());
        if (!enabled) CHECK(analysis.dense_requests.empty());
        const auto native = CP::slice(hybrid.core, configs, 0, -1, {}, analysis.dense_requests);
        INFO(native.error);
        REQUIRE(native.ok());
        CHECK(native.dense_requests_applied == 0);
        CHECK(native.dense_modifier_volumes == 0);
        CHECK(native.applied_dense_regions.empty());
        CHECK(lower_segments(native, std::numeric_limits<double>::infinity()) == expected);
        CHECK_THAT(amounts(native).supplied, WithinAbs(amounts(baseline).supplied, 0.));
    }
}

TEST_CASE("The saved curved project retains its fixed raw comparison and separately audits actual native interfaces", "[FormorixUnderlay]")
{
    ScopedSlic3rTemporaryDir temporary("formorix-underlay-saved-project");
    DynamicPrintConfig saved;
    ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Disable);
    Model model = Model::read_from_file(std::string(TEST_DATA_DIR) + "/formorix/hybrid-curved-saved-profile.3mf", &saved,
                                       &substitutions, LoadStrategy::LoadModel | LoadStrategy::LoadConfig | LoadStrategy::AddDefaultInstances);
    auto config = DynamicPrintConfig::full_print_config();
    config.apply(saved);
    CHECK_THAT(config.opt_float("filament_flow_ratio", 0), WithinAbs(.98, 0.));
    const auto configs = sources(config);
    std::string error;
    const auto project = FS::Project::decode(model.formorix_surfaces, error);
    INFO(error);
    REQUIRE(project);
    REQUIRE_FALSE(model.objects.empty());
    REQUIRE(project->regions.size() == 1);
    CHECK_FALSE(project->regions[0].settings.dense_underlay);
    const auto input = FH::extract(*model.objects.front(), 0, *project,
                                   {configs[0].process, configs[1].process, configs[2].process});
    auto generated = FH::generate(input);
    INFO(generated.error);
    REQUIRE(generated.ok());
    REQUIRE(generated.regions.size() == 1);
    // Keep the historical 22,400 normal-offset points frozen for this RAW-only
    // geometry comparison. They are not substituted for the actual shared seam.
    auto raw_before = generated;
    REQUIRE(raw_before.regions[0].interface_samples.size() == 22400);
    const auto native_before = CP::slice(generated.core, configs, 0, -1);
    INFO(native_before.error);
    REQUIRE(native_before.ok());
    FH::audit_underlay(raw_before, segments(native_before, true));
    CHECK(raw_before.regions[0].underlay.missing == 6006);
    CHECK(raw_before.regions[0].underlay.raw_missing == 6006);

    auto actual_before = generated;
    // Only this in-memory copy explicitly asks for local 100% underlay. The
    // saved project, source material flow and all other process options remain.
    actual_before.regions[0].settings.dense_underlay = true;
    actual_before.regions[0].settings.underlay_density = 100.;
    const auto prepared = FU::prepare(actual_before);
    INFO(prepared.error);
    REQUIRE(prepared.ok());
    REQUIRE(prepared.regions.size() == 1);
    REQUIRE_FALSE(prepared.regions[0].probes.empty());
    FH::audit_underlay(actual_before, segments(native_before, false));
    const auto requested = FU::analyze(actual_before, prepared, native_before);
    INFO(requested.error);
    REQUIRE(requested.ok());
    REQUIRE_FALSE(requested.dense_requests.empty());
    const auto native_after = CP::slice(generated.core, configs, 0, -1, {}, requested.dense_requests);
    INFO(native_after.error);
    REQUIRE(native_after.ok());
    REQUIRE(native_after.dense_modifier_volumes > 0);
    auto raw_after = generated;
    FH::audit_underlay(raw_after, segments(native_after, true));
    CHECK(raw_after.regions[0].interface_samples == raw_before.regions[0].interface_samples);
    CHECK(raw_after.regions[0].underlay.samples == 22400);
    CHECK(raw_after.regions[0].underlay.missing < raw_before.regions[0].underlay.missing);
    auto actual_after = actual_before;
    FH::audit_underlay(actual_after, segments(native_after, false));
    const auto revised = FU::analyze(actual_after, prepared, native_after);
    INFO(revised.error);
    REQUIRE(revised.ok());
    CHECK(actual_after.regions[0].interface_samples == actual_before.regions[0].interface_samples);
    CHECK(actual_after.regions[0].underlay.unknown_geometry > 0);
    CHECK_FALSE(actual_after.regions[0].underlay.coverage_bounded);
    CHECK_FALSE(revised.regions[0].coverage_bounded);
    CHECK_FALSE(project->regions[0].settings.dense_underlay);
    CHECK(config.diff(configs[0].config).empty());
    check_native_flow(native_before, .98);
    check_native_flow(native_after, .98);
    report("saved_raw_before", native_before, raw_before);
    report("saved_raw_dense_100", native_after, raw_after);
    report("saved_actual_before_strict", native_before, actual_before);
    report("saved_actual_dense_100_strict", native_after, actual_after);
    std::cout << "UNDERLAY_SAVED_AREAS,interface_mm2=" << revised.regions[0].interface_area
              << ",raw_missing_before_mm2=" << requested.regions[0].raw_missing_area
              << ",raw_missing_after_mm2=" << revised.regions[0].raw_missing_area
              << ",native_layer_gap_after_mm2=" << revised.regions[0].native_layer_gap_area
              << ",unknown_after_mm2=" << revised.regions[0].unknown_area << '\n';
}
