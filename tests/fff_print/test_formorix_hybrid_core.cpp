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
#include <stdexcept>
#include <string>
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
                                         {"before_layer_change_gcode", "G92 E0"},
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
        }
    CHECK(found);
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
