#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixCorePreview.hpp"
#include "libslic3r/FormorixGeometry.hpp"
#include "libslic3r/FormorixHybrid.hpp"
#include "libslic3r/FormorixSurfaces.hpp"
#include "libslic3r/FormorixProcess.hpp"
#include "libslic3r/Format/ProjectStrategy.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/GCode.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "test_helpers.hpp"
#include "test_utils.hpp"
#include <boost/filesystem/operations.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
namespace CP = Slic3r::FormorixCorePreview;
namespace FG = Slic3r::FormorixGeometry;
using Catch::Matchers::WithinAbs;
namespace {
std::array<CP::Source, 3> sources()
{
    std::array<CP::Source, 3> out;
    for (int h = 0; h < 3; ++h) {
        auto& s           = out[h];
        auto& p           = s.process;
        p.material_id     = std::array<std::string, 3>{"PETG", "PVA", "PETG-CF"}[h];
        p.nozzle_profile  = "nozzle-" + std::to_string(h);
        p.nozzle_diameter = std::array<double, 3>{.8, .4, .6}[h];
        p.line_width      = p.nozzle_diameter * 1.1;
        p.layer_height    = .2;
        p.speed           = 30. + 10. * h;
        s.config          = DynamicPrintConfig::full_print_config();
        s.config.set_deserialize_strict({{"layer_height", "0.2"},
                                         {"initial_layer_print_height", "0.2"},
                                         {"sparse_infill_density", "20%"},
                                         {"enable_support", "0"},
                                         {"brim_type", "no_brim"},
                                         {"skirt_loops", "0"},
                                         {"elefant_foot_compensation", "0"}});
        for (const char* key : {"line_width", "outer_wall_line_width", "inner_wall_line_width", "sparse_infill_line_width",
                                "internal_solid_infill_line_width", "top_surface_line_width"})
            s.config.set_deserialize_strict(key, std::to_string(p.line_width));
    }
    return out;
}
FG::Mesh geometry(const TriangleMesh& mesh)
{
    FG::Mesh out;
    out.faces = mesh.its.indices;
    for (const auto& p : mesh.its.vertices)
        out.vertices.push_back(p.cast<double>());
    return out;
}
} // namespace

TEST_CASE("An immutable hybrid preview can slice but cannot export after configuration changes", "[FormorixHybridCore]")
{
    Print print(Print::Purpose::HybridPreview);
    auto config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict("formorix_5d_enabled", "1");
    Model model;
    Test::init_print({Test::cube(8.)}, print, model, config);
    print.process();
    REQUIRE_FALSE(print.objects().empty());
    REQUIRE_FALSE(print.objects().front()->layers().empty());
    ScopedTemporaryFile output(".gcode");
    GCode generator;
    for (bool reset : {false, true}) {
        if (reset) {
            print.clear();
            config.set_deserialize_strict("formorix_5d_enabled", "0");
            print.apply(model, config);
        }
        CHECK(print.hybrid_preview_only());
        CHECK_THROWS_AS(print.export_gcode(output.string(), nullptr), std::runtime_error);
        CHECK_THROWS_AS(print.export_gcode_from_previous_file(output.string(), nullptr, {}), std::runtime_error);
        CHECK_THROWS_AS(generator.do_export(&print, output.string().c_str(), nullptr, {}), std::runtime_error);
        CHECK_THROWS_AS(print.export_cached_data(output.string()), std::runtime_error);
        CHECK_FALSE(boost::filesystem::exists(output.path()));
    }
}
TEST_CASE("Only immutable previews omit E-addressing export macro validation", "[FormorixHybridCore]")
{
    for (const auto& input : std::array<std::pair<bool, std::string>, 3>{{{true, ""}, {true, "g92 e0"}, {false, "G92 E0"}}}) {
        auto config = DynamicPrintConfig::full_print_config();
        config.set_deserialize_strict("gcode_flavor", "marlin");
        config.set_deserialize_strict("use_relative_e_distances", input.first ? "1" : "0");
        config.set_deserialize_strict("before_layer_change_gcode", input.second);
        config.set_deserialize_strict("layer_change_gcode", "");
        Print standard, preview(Print::Purpose::HybridPreview);
        Model standard_model, preview_model;
        Test::init_print({Test::cube(8.)}, standard, standard_model, config);
        Test::init_print({Test::cube(8.)}, preview, preview_model, config);
        CHECK_FALSE(standard.validate().string.empty());
        CHECK(preview.validate().string.empty());
        CHECK_THROWS_AS(preview.require_executable(), std::runtime_error);
        config.set_deserialize_strict("layer_height", "1.0");
        preview.apply(preview_model, config);
        CHECK_FALSE(preview.validate().string.empty());
    }
}
TEST_CASE("Native Orca core paths retain source coordinates and head-specific extrusion metadata", "[FormorixHybridCore]")
{
    auto core = geometry(Test::cube(10.));
    for (auto& p : core.vertices)
        p += Vec3d(1200.123456789, 40.234567891, 5.123456789);
    auto configs      = sources();
    const auto result = CP::slice(core, configs, 0, 0);
    INFO(result.error);
    REQUIRE(result.ok());
    CHECK(result.float_conversion_bound < .00001);
    CHECK(std::abs(result.converted_volume_residual) < .0001);
    bool walls = false, infill = false, solid = false;
    for (const auto& p : result.paths) {
        CHECK(p.head == 0);
        CHECK(p.process.material_id == "PETG");
        CHECK_THAT(p.process.nozzle_diameter, WithinAbs(.8, 1e-12));
        CHECK_THAT(p.width, WithinAbs(p.process.line_width, 1e-12));
        walls |= is_perimeter(p.role);
        infill |= p.role == erInternalInfill;
        solid |= is_solid_infill(p.role);
        for (const auto& x : p.points) {
            CHECK(x.x() >= 1200.);
            CHECK(x.x() <= 1211.);
            CHECK(x.z() > 5.);
            CHECK(x.z() <= 15.123457);
        }
    }
    CHECK(walls);
    CHECK(infill);
    CHECK(solid);
    CHECK_THAT(result.local_origin.z(), WithinAbs(5.123456789, 1e-12));
    const auto other = CP::slice(core, configs, 2, 2);
    INFO(other.error);
    REQUIRE(other.ok());
    CHECK_THAT(other.paths.front().process.nozzle_diameter, WithinAbs(.6, 1e-12));
    CHECK(other.paths.front().process.material_id == "PETG-CF");
    CHECK(std::abs(other.paths.front().width - result.paths.front().width) > .01);
}
TEST_CASE("Native support paths use their independent support head configuration", "[FormorixHybridCore]")
{
    // A sloping overhang fixture from the existing Orca test model collection.
    auto core    = geometry(Test::mesh(Test::TestMesh::overhang));
    auto configs = sources();
    for (auto& s : configs)
        s.config.set_deserialize_strict({{"enable_support", "1"}, {"support_threshold_angle", "60"}, {"support_type", "normal(auto)"}});
    configs[1].process.flow_ratio = .91;
    configs[1].process.surface_flow_ratio = 1.7; // 5D outer-wall metadata must not affect native support.
    configs[1].config.set_deserialize_strict({{"print_flow_ratio", "1.13"}, {"set_other_flow_ratios", "1"},
                                            {"support_flow_ratio", "0.66"}, {"support_interface_flow_ratio", "0.77"},
                                            {"first_layer_flow_ratio", "1.15"}});
    const auto result = CP::slice(core, configs, 0, 1);
    INFO(result.error);
    REQUIRE(result.ok());
    bool found = false;
    for (const auto& path : result.paths)
        if (path.support) {
            found = true;
            CHECK(path.head == 1);
            CHECK(path.process.material_id == "PVA");
            CHECK_THAT(path.process.nozzle_diameter, WithinAbs(.4, 1e-12));
            CHECK(path.width > 0.);
            const double role_factor = path.role == erSupportMaterial ? .66 : path.role == erSupportMaterialInterface ? .77 : 1.;
            CHECK_THAT(path.role_flow_ratio, WithinAbs(role_factor, 1e-12));
            CHECK_THAT(path.process.surface_flow_ratio, WithinAbs(1., 1e-12));
            CHECK_THAT(path.nominal_supplied_mm3_per_mm,
                       WithinAbs(path.mm3_per_mm * .91 * 1.13 * role_factor * path.first_layer_flow_ratio, 1e-12));
        }
    CHECK(found);
}
TEST_CASE("Native flow metadata applies export ratios once without changing Orca geometry", "[FormorixHybridCore]")
{
    const auto core = geometry(Test::cube(8.));
    const auto baseline = CP::slice(core, sources(), 0, -1);
    INFO(baseline.error);
    REQUIRE(baseline.ok());
    const std::vector<std::pair<ExtrusionRole, double>> optional_role_factors{
        {erExternalPerimeter, .82}, {erPerimeter, .88}, {erInternalInfill, .93}, {erSolidInfill, 1.04}, {erGapFill, .97}};
    const std::vector<std::pair<ExtrusionRole, double>> always_role_factors{
        {erTopSolidInfill, .79}, {erBottomSurface, 1.21}, {erInternalBridgeInfill, 1.07}};
    for (bool enabled : {false, true}) {
        auto configs = sources();
        auto& source = configs[0];
        source.process.flow_ratio = .91;
        source.process.print_flow_ratio = .77; // The actual projected Orca pass is authoritative.
        source.process.surface_flow_ratio = 1.7;
        source.process.local_width_limits = FormorixProcess::WidthLimits{.8, .95};
        source.process.volumetric_limit = .2;
        source.config.set_deserialize_strict({{"print_flow_ratio", "1.13"},
                                             {"set_other_flow_ratios", enabled ? "1" : "0"},
                                             {"outer_wall_flow_ratio", "0.82"}, {"inner_wall_flow_ratio", "0.88"},
                                             {"sparse_infill_flow_ratio", "0.93"}, {"internal_solid_infill_flow_ratio", "1.04"},
                                             {"gap_fill_flow_ratio", "0.97"}, {"top_solid_infill_flow_ratio", "0.79"},
                                             {"bottom_solid_infill_flow_ratio", "1.21"}, {"internal_bridge_flow", "1.07"},
                                             {"first_layer_flow_ratio", "1.15"}});
        const auto result = CP::slice(core, configs, 0, -1);
        INFO(result.error);
        REQUIRE(result.ok());
        REQUIRE(result.paths.size() == baseline.paths.size());
        bool external = false, sparse = false, top = false, bottom = false, first_layer = false;
        for (std::size_t i = 0; i < result.paths.size(); ++i) {
            const auto& path = result.paths[i];
            const auto& original = baseline.paths[i];
            CHECK(path.points == original.points);
            CHECK(path.role == original.role);
            CHECK_THAT(path.width, WithinAbs(original.width, 1e-12));
            CHECK_THAT(path.height, WithinAbs(original.height, 1e-12));
            CHECK_THAT(path.mm3_per_mm, WithinAbs(original.mm3_per_mm, 1e-12));
            double role_factor = 1.;
            for (const auto& factor : optional_role_factors)
                if (enabled && path.role == factor.first)
                    role_factor = factor.second;
            for (const auto& factor : always_role_factors)
                if (path.role == factor.first)
                    role_factor = factor.second;
            const bool on_first = std::abs(path.points.front().z() - .2) < 1e-6;
            const double first_factor = enabled && on_first ? 1.15 : 1.;
            CHECK_THAT(path.role_flow_ratio, WithinAbs(role_factor, 1e-12));
            CHECK_THAT(path.first_layer_flow_ratio, WithinAbs(first_factor, 1e-12));
            CHECK_THAT(path.process.flow_ratio, WithinAbs(.91, 1e-12));
            CHECK_THAT(path.process.print_flow_ratio, WithinAbs(1.13, 1e-12));
            CHECK_THAT(path.process.surface_flow_ratio, WithinAbs(1., 1e-12));
            CHECK_FALSE(path.process.local_width_limits);
            CHECK(path.flow_geometry_unverified);
            const double expected_supply = original.mm3_per_mm * .91 * 1.13 * role_factor * first_factor;
            CHECK_THAT(path.nominal_supplied_mm3_per_mm, WithinAbs(expected_supply, 1e-12));
            CHECK_THAT(path.process.speed * path.nominal_supplied_mm3_per_mm, WithinAbs(.2, 1e-10));
            external |= path.role == erExternalPerimeter;
            sparse |= path.role == erInternalInfill;
            top |= path.role == erTopSolidInfill;
            bottom |= path.role == erBottomSurface;
            first_layer |= on_first;
        }
        CHECK(external);
        CHECK(sparse);
        CHECK(top);
        CHECK(bottom);
        CHECK(first_layer);
        CHECK_THAT(source.process.print_flow_ratio, WithinAbs(.77, 1e-12));
        CHECK_THAT(source.process.surface_flow_ratio, WithinAbs(1.7, 1e-12));
        REQUIRE(source.process.local_width_limits);
        CHECK_THAT(source.process.local_width_limits->minimum_mm, WithinAbs(.8, 1e-12));
    }
}
TEST_CASE("Zero native role flow retains display geometry without claiming material supply", "[FormorixHybridCore]")
{
    auto configs = sources();
    configs[0].config.set_deserialize_strict("top_solid_infill_flow_ratio", "0");
    const auto result = CP::slice(geometry(Test::cube(8.)), configs, 0, -1);
    INFO(result.error);
    REQUIRE(result.ok());
    bool top = false;
    for (const auto& path : result.paths)
        if (path.role == erTopSolidInfill) {
            top = true;
            CHECK(path.mm3_per_mm > 0.);
            CHECK_THAT(path.nominal_supplied_mm3_per_mm, WithinAbs(0., 1e-15));
            CHECK(path.flow_geometry_unverified);
            CHECK(std::isfinite(path.process.speed));
        }
    CHECK(top);
}
TEST_CASE("An explicitly selected second nozzle retains its process variant in the isolated core pass", "[FormorixHybridCore]")
{
    auto configs = sources();
    auto& source = configs[0];
    source.config.set_num_extruders(2);
    source.config.set_key_value("nozzle_diameter", new ConfigOptionFloats{.4, .8});
    source.config.set_key_value("outer_wall_speed", new ConfigOptionFloats{12., 21.});
    source.config.set_key_value("filament_map", new ConfigOptionInts{2});
    source.config.set_key_value("filament_nozzle_map", new ConfigOptionInts{2});
    source.nozzle_index = 1;
    const auto result   = CP::slice(geometry(Test::cube(8.)), configs, 0, -1);
    INFO(result.error);
    REQUIRE(result.ok());
    bool found = false;
    for (const auto& path : result.paths)
        if (path.role == erExternalPerimeter) {
            found = true;
            CHECK_THAT(path.process.nozzle_diameter, WithinAbs(.8, 1e-12));
            CHECK_THAT(path.process.speed, WithinAbs(21., 1e-12));
        }
    CHECK(found);
}
TEST_CASE("Invalid and canceled hybrid core requests retain no partial paths", "[FormorixHybridCore]")
{
    auto core    = geometry(Test::cube(8.));
    auto configs = sources();
    CHECK_FALSE(CP::slice(core, configs, 0, 0, [] { return true; }).ok());
    core.faces.pop_back();
    CHECK_FALSE(CP::slice(core, configs, 0, 0).ok());
    core                               = geometry(Test::cube(8.));
    configs[0].process.nozzle_diameter = -1.;
    CHECK_FALSE(CP::slice(core, configs, 0, 0).ok());
}

TEST_CASE("Nonneutral shrink compensation cannot silently separate hybrid core and shell", "[FormorixHybridCore]")
{
    const auto core = geometry(Test::cube(8.));
    for (const char* key : {"filament_shrink", "filament_shrinkage_compensation_z"}) {
        INFO(key);
        auto configs = sources();
        configs[0].config.set_key_value(key, new ConfigOptionPercents{98.});
        const auto result = CP::slice(core, configs, 0, -1);
        CHECK_FALSE(result.ok());
        CHECK(result.paths.empty());
        CHECK(result.error.find("100%") != std::string::npos);
        configs = sources();
        configs[0].config.set_deserialize_strict("enable_support", "1");
        configs[1].config.set_key_value(key, new ConfigOptionPercents{98.});
        CHECK_FALSE(CP::slice(core, configs, 0, 1).ok());
    }
    auto configs                              = sources();
    configs[0].process.shrink_compensation[0] = 1.02;
    CHECK_FALSE(CP::slice(core, configs, 0, -1).ok());
}

TEST_CASE("Marked shell partition feeds actual Orca core slicing and actual underlay evidence", "[FormorixHybridCore]")
{
    namespace FH = FormorixHybrid;
    FH::Input input;
    input.solid         = geometry(Test::cube(10.));
    const auto original = input.solid;
    auto configs        = sources();
    FH::RegionInput region;
    region.id                    = 1;
    region.head                  = 2;
    region.process               = configs[2].process;
    region.surface               = {{{0., 0., 10.}, {10., 0., 10.}, {10., 10., 10.}, {0., 10., 10.}}, {{0, 1, 2}, {0, 2, 3}}};
    region.settings.layers       = 3;
    region.settings.layer_height = .2;
    region.cad_reference         = FG::plane({0., 0., 10.}, Vec3d::UnitZ());
    input.regions.push_back(region);
    auto hybrid = FH::generate(input);
    INFO(hybrid.error);
    REQUIRE(hybrid.ok());
    CHECK_THAT(FG::volume(hybrid.core), WithinAbs(940., 1e-5));
    CHECK_THAT(hybrid.partition.volume_residual, WithinAbs(0., 1e-5));
    CHECK(input.solid.faces == original.faces);
    CHECK(input.solid.vertices == original.vertices);
    auto core = CP::slice(hybrid.core, configs, 0, -1);
    INFO(core.error);
    REQUIRE(core.ok());
    std::vector<FH::UnderlaySegment> sweeps;
    bool wall = false, fill = false;
    for (const auto& path : core.paths) {
        wall |= is_perimeter(path.role);
        fill |= is_infill(path.role);
        for (std::size_t i = 1; i < path.points.size(); ++i) {
            CHECK(path.points[i].z() <= 9.400001);
            sweeps.push_back(
                {path.points[i - 1], path.points[i], path.width, path.height, path.head, path.process.material_id, path.support});
        }
    }
    CHECK(wall);
    CHECK(fill);
    REQUIRE_FALSE(sweeps.empty());
    FH::audit_underlay(hybrid, sweeps);
    CHECK(hybrid.regions.front().underlay.samples > 0);
    // Nominal sampling cannot turn unknown footprint/adhesion into a proof.
    CHECK_FALSE(hybrid.regions.front().underlay.coverage_bounded);
    CHECK_FALSE(hybrid.regions.front().accuracy.verified());
    configs[0].config.set_deserialize_strict("enable_support", "1");
    CHECK_FALSE(CP::slice(hybrid.core, configs, 0, -1).ok());
}

TEST_CASE("A saved painted curved surface feeds a genuinely curved partitioned core through Orca", "[FormorixHybridCore]")
{
    namespace FH = FormorixHybrid;
    ScopedSlic3rTemporaryDir temporary("formorix-hybrid-core-project");
    DynamicPrintConfig config;
    ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Disable);
    Model model = Model::read_from_file(std::string(TEST_DATA_DIR) + "/../../docs/formorix/examples/curved-wall.3mf", &config,
                                        &substitutions,
                                        LoadStrategy::LoadModel | LoadStrategy::LoadConfig | LoadStrategy::AddDefaultInstances);
    std::string error;
    auto project = FormorixSurfaces::Project::decode(model.formorix_surfaces, error);
    INFO(error);
    REQUIRE(project);
    REQUIRE_FALSE(model.objects.empty());
    const auto configs = sources();
    auto input         = FH::extract(*model.objects.front(), 0, *project, {configs[0].process, configs[1].process, configs[2].process});
    REQUIRE(input.regions.size() == 2);
    // The second marking has a perpendicular, overlapping shell. This test
    // uses the existing curved marking alone, without retessellating its paint.
    input.regions.resize(1);
    const auto original = input.solid;
    const auto hybrid   = FH::generate(input);
    INFO(hybrid.error);
    REQUIRE(hybrid.ok());
    CHECK(FG::volume(hybrid.core) < FG::volume(original));
    const auto conventional = CP::slice(hybrid.core, configs, 0, -1);
    INFO(conventional.error);
    REQUIRE(conventional.ok());
    CHECK(std::any_of(conventional.paths.begin(), conventional.paths.end(), [](const auto& p) { return is_perimeter(p.role); }));
    CHECK(std::any_of(conventional.paths.begin(), conventional.paths.end(), [](const auto& p) { return is_infill(p.role); }));
    CHECK(input.solid.vertices == original.vertices);
    CHECK(input.solid.faces == original.faces);
}

TEST_CASE("The saved GUI Formorix process produces native core paths without layer G-code macros", "[FormorixHybridCore]")
{
    namespace FH = FormorixHybrid;
    ScopedSlic3rTemporaryDir temporary("formorix-hybrid-saved-profile");
    DynamicPrintConfig saved;
    ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Disable);
    Model model = Model::read_from_file(std::string(TEST_DATA_DIR) + "/formorix/hybrid-curved-saved-profile.3mf", &saved, &substitutions,
                                        LoadStrategy::LoadModel | LoadStrategy::LoadConfig | LoadStrategy::AddDefaultInstances);
    auto config = DynamicPrintConfig::full_print_config();
    config.apply(saved);
    REQUIRE(config.opt_bool("use_relative_e_distances"));
    REQUIRE(config.opt_string("before_layer_change_gcode").empty());
    REQUIRE(config.opt_string("layer_change_gcode").empty());
    REQUIRE(config.opt_string("printer_settings_id") == "Formorix 5D 0.4 nozzle");
    const double nozzle = config.opt_float("nozzle_diameter", 0);
    double width        = config.get_abs_value("outer_wall_line_width", nozzle);
    if (width == 0.)
        width = config.get_abs_value("line_width", nozzle);
    if (width == 0.)
        width = nozzle * 1.05;
    const auto profile = FormorixProcess::from_orca(config, 0, 0, "Generic PLA @System", "Formorix 5D 0.4 nozzle", width,
                                                    config.opt_float("layer_height"), config.opt_float("outer_wall_speed", 0));
    std::array<CP::Source, 3> configs;
    for (auto& source : configs) {
        source.config  = config;
        source.process = profile;
    }
    std::string error;
    auto project = FormorixSurfaces::Project::decode(model.formorix_surfaces, error);
    INFO(error);
    REQUIRE(project);
    REQUIRE_FALSE(model.objects.empty());
    const auto input = FH::extract(*model.objects.front(), 0, *project, {profile, profile, profile});
    auto hybrid      = FH::generate(input);
    INFO(hybrid.error);
    REQUIRE(hybrid.ok());
    const auto core = CP::slice(hybrid.core, configs, 0, -1);
    INFO(core.error);
    REQUIRE(core.ok());
    CHECK(std::any_of(core.paths.begin(), core.paths.end(), [](const auto& p) { return is_perimeter(p.role); }));
    CHECK(std::any_of(core.paths.begin(), core.paths.end(), [](const auto& p) { return is_infill(p.role); }));
    CHECK(config.opt_string("before_layer_change_gcode").empty());
    CHECK(config.opt_string("layer_change_gcode").empty());
    std::vector<FH::UnderlaySegment> underlay;
    for (const auto& path : core.paths)
        for (std::size_t i = 1; i < path.points.size(); ++i)
            underlay.push_back({path.points[i - 1], path.points[i], path.width, path.height, path.head,
                               path.process.material_id, path.support, path.role == erInternalInfill, is_bridge(path.role)});
    FH::audit_underlay(hybrid, underlay);
    const auto& audit = hybrid.regions.front().underlay;
    CHECK(audit.samples > 0);
    CHECK(audit.missing == audit.missing_core + audit.unsuitable_infill + audit.path_gaps + audit.unknown_geometry);
    std::cout << "SAVED_CURVED_UNDERLAY,samples=" << audit.samples << ",missing=" << audit.missing
              << ",missing_core=" << audit.missing_core << ",excluded_infill=" << audit.unsuitable_infill
              << ",path_gaps=" << audit.path_gaps << ",unknown=" << audit.unknown_geometry
              << ",max_finite_gap=" << audit.maximum_finite_gap << ",no_contact_within_0.5=" << audit.no_nearby_contact << "\n";
}

TEST_CASE("Conventional underlay uses Orca rounded rectangles and finite segment endpoints", "[FormorixHybridCore]")
{
    namespace FH = FormorixHybrid;
    FH::Result result;
    result.original = result.core = geometry(Test::cube(4.));
    result.regions.resize(1);
    auto& region = result.regions.front();
    region.process.layer_height = .2;
    // At y=2.3 the .8 x .2 rounded rectangle still has a flat top at z=4.
    // The old ellipse reported a .07386 mm gap instead of the actual .04 mm.
    region.interface_samples = {{2., 2.3, 4.04}, {3.2, 2., 4.}};
    region.interface_normals = {Vec3d::UnitZ(), Vec3d::UnitZ()};
    FH::audit_underlay(result, {{{1., 2., 4.}, {3., 2., 4.}, .8, .2, 0, "PETG", false, false}});
    CHECK(region.underlay.samples == 2);
    CHECK(region.underlay.missing == 1);
    CHECK(region.underlay.path_gaps == 1);
    CHECK_THAT(region.underlay.maximum_finite_gap, WithinAbs(.04, 1e-10));
    CHECK(region.underlay.no_nearby_contact == 1);
    CHECK(result.path_gaps.front().isApprox(Vec3d(3.2, 2., 4.)));
    CHECK_FALSE(region.underlay.coverage_bounded);
}

TEST_CASE("Hanging bridge geometry does not become certified conventional underlay", "[FormorixHybridCore]")
{
    namespace FH = FormorixHybrid;
    FH::Result result;
    result.original = result.core = geometry(Test::cube(4.));
    result.regions.resize(1);
    auto& region = result.regions.front();
    region.process.layer_height = .2;
    region.interface_samples = {{2., 2., 4.}};
    region.interface_normals = {Vec3d::UnitZ()};
    FH::audit_underlay(result, {{{1., 2., 4.}, {3., 2., 4.}, .4, .2, 1, "PVA", false, false, true}});
    CHECK(region.underlay.missing == 1);
    CHECK(region.underlay.unknown_geometry == 1);
    CHECK(region.underlay.path_gaps == 0);
}

TEST_CASE("Underlay diagnostics separate absent core excluded infill and uncovered core paths", "[FormorixHybridCore]")
{
    namespace FH = FormorixHybrid;
    FH::Result result;
    result.original = result.core = geometry(Test::cube(4.));
    result.regions.resize(1);
    auto& region = result.regions.front();
    region.process.layer_height = .2;
    region.settings.use_infill = false;
    region.interface_samples = {{2., 2., 4.}, {2., 1., 4.}, {5., 2., 4.}};
    region.interface_normals.assign(3, Vec3d::UnitZ());
    const std::vector<FH::UnderlaySegment> infill{{{1., 2., 4.}, {3., 2., 4.}, .8, .2, 0, "PETG", false, true}};
    FH::audit_underlay(result, infill);
    CHECK(region.underlay.missing == 3);
    CHECK(region.underlay.missing_core == 1);
    CHECK(region.underlay.unsuitable_infill == 1);
    CHECK(region.underlay.path_gaps == 1);
    CHECK(region.underlay.unknown_geometry == 0);
    CHECK(result.missing_core.size() == 1);
    CHECK(result.unsuitable_infill.size() == 1);
    CHECK(result.path_gaps.size() == 1);
    region.settings.use_infill = true;
    FH::audit_underlay(result, infill);
    CHECK(region.underlay.missing == 2);
    CHECK(region.underlay.unsuitable_infill == 0);
    CHECK(result.unsuitable_infill.empty());
    CHECK_FALSE(region.underlay.coverage_bounded);
}

TEST_CASE("Missing geometry stays unknown while actual support can provide nominal contact", "[FormorixHybridCore]")
{
    namespace FH = FormorixHybrid;
    FH::Result result;
    result.regions.resize(1);
    auto& region = result.regions.front();
    region.process.layer_height = .2;
    region.interface_samples = {{2., 2., 4.}};
    region.interface_normals = {Vec3d::UnitZ()};
    FH::audit_underlay(result, {});
    CHECK(region.underlay.unknown_geometry == 1);
    CHECK(region.underlay.missing_core == 0);
    FH::audit_underlay(result, {{{1., 2., 4.}, {3., 2., 4.}, .4, .2, 1, "PVA", true, false}});
    CHECK(region.underlay.missing == 0);
    CHECK(result.unknown_underlay.empty());
    CHECK_FALSE(region.underlay.coverage_bounded);
}

TEST_CASE("Underlay rays respect finite endcaps in both travel directions", "[FormorixHybridCore]")
{
    namespace FH = FormorixHybrid;
    FH::Result result;
    result.regions.resize(1);
    auto& region = result.regions.front();
    region.process.layer_height = .2;
    region.interface_samples = {{1.02, 0., .9}, {1.02, 0., .9}};
    region.interface_normals = {Vec3d::UnitX(), -Vec3d::UnitX()};
    FH::audit_underlay(result, {{{0., 0., 1.}, {1., 0., 1.}, .8, .2, 0, "PETG", false, false}});
    CHECK(region.underlay.samples == 2);
    CHECK(region.underlay.missing == 1);
    CHECK(result.missing_underlay.size() == 1);
    region.interface_samples.resize(1);
    region.interface_normals.resize(1);
    FH::audit_underlay(result, {{{0., 0., .95}, {1., 0., 1.}, .8, .2, 0, "PETG", false, false}});
    CHECK(region.underlay.missing == 1);
    CHECK(region.underlay.unknown_geometry == 1);
}
