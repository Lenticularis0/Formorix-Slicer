#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixProfiles.hpp"
#include "libslic3r/FormorixSurfaces.hpp"
#include "libslic3r/FormorixProcess.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <cmath>
#include <string>
#include <vector>

using namespace Slic3r;
namespace F = Slic3r::FormorixProfiles;
namespace S = Slic3r::FormorixSurfaces;
using Catch::Matchers::WithinAbs;

namespace {
void configure(PresetBundle& bundle)
{
    auto& printer = bundle.printers.get_edited_preset();
    printer.name  = "Active Formorix machine";
    printer.config.set_key_value("nozzle_diameter", new ConfigOptionFloats{.4});
    printer.config.set_key_value("formorix_5d_enabled", new ConfigOptionBool(true));
    auto& process = bundle.prints.get_edited_preset();
    process.name  = "Current process";
    process.config.set_key_value("outer_wall_line_width", new ConfigOptionFloatOrPercent(110., true));
    process.config.set_key_value("outer_wall_speed", new ConfigOptionFloatsNullable{70.});
    process.config.set_key_value("layer_height", new ConfigOptionFloat(.2));
    const double diameters[]{.8, .4, .6};
    const char* names[]{"PETG", "PVA", "PETG-CF"};
    const int temperatures[]{240, 205, 250};
    for (int h = 0; h < 3; ++h) {
        auto nozzle = printer.config;
        nozzle.set_key_value("nozzle_diameter", new ConfigOptionFloats{diameters[h]});
        nozzle.set_key_value("printable_height", new ConfigOptionFloat(100.));
        bundle.printers.load_preset("", "Nozzle " + std::to_string(h), nozzle, false);
        auto material = bundle.filaments.get_edited_preset().config;
        material.set_key_value("nozzle_temperature", new ConfigOptionInts{temperatures[h]});
        material.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats{10. + h});
        material.set_key_value("filament_flow_ratio", new ConfigOptionFloats{1. + .01 * h});
        bundle.filaments.load_preset("", names[h], material, false).filament_id = std::string("material-") + names[h];
    }
}
S::Project assignments()
{
    S::Project project;
    const char* names[]{"PETG", "PVA", "PETG-CF"};
    for (int h = 0; h < 3; ++h) {
        project.heads[h].material_preset = names[h];
        project.heads[h].assign_nozzle("Nozzle " + std::to_string(h), 0);
    }
    return project;
}
} // namespace
TEST_CASE("Current Orca sources produce independent nozzle and material process values", "[FormorixProfiles]")
{
    PresetBundle bundle;
    configure(bundle);
    auto project = assignments();
    const double diameters[]{.8, .4, .6};
    const double temperatures[]{240., 205., 250.};
    const char* material_ids[]{"material-PETG", "material-PVA", "material-PETG-CF"};
    for (int h = 0; h < 3; ++h) {
        const auto fresh = F::refresh(bundle, project.heads[h]);
        CHECK(fresh.process.material_id == material_ids[h]);
        CHECK(fresh.process.nozzle_profile == "Nozzle " + std::to_string(h) + " / 0");
        CHECK_THAT(fresh.process.nozzle_diameter, WithinAbs(diameters[h], 1e-12));
        CHECK_THAT(fresh.process.line_width, WithinAbs(diameters[h] * 1.1, 1e-12));
        CHECK_THAT(fresh.process.temperature, WithinAbs(temperatures[h], 1e-12));
        CHECK_THAT(fresh.process.volumetric_limit, WithinAbs(10. + h, 1e-12));
        CHECK_THAT(fresh.process.flow_ratio, WithinAbs(1. + .01 * h, 1e-12));
        CHECK_THAT(fresh.process.layer_height, WithinAbs(.2, 1e-12));
        CHECK_THAT(fresh.process.speed, WithinAbs(70., 1e-12));
        CHECK(fresh.config.opt_bool("formorix_5d_enabled"));
        CHECK_THAT(fresh.config.opt_float("printable_height"),
                   WithinAbs(bundle.printers.get_edited_preset().config.opt_float("printable_height"), 1e-12));
        const double surface_height = .1 + .05 * h;
        const auto surface          = F::resolve(F::source(bundle, project.heads[h], surface_height));
        CHECK_THAT(surface.process.layer_height, WithinAbs(surface_height, 1e-12));
        REQUIRE(project.heads[h].process);
    }
}
TEST_CASE("Same-name Orca edits invalidate snapshots and resolution never trusts cached values", "[FormorixProfiles]")
{
    PresetBundle bundle;
    configure(bundle);
    auto project = assignments();
    std::array<std::string, 3> signatures;
    F::invalidate_changed(project, bundle, signatures);
    for (auto& head : project.heads)
        F::refresh(bundle, head);
    CHECK_FALSE(F::invalidate_changed(project, bundle, signatures));
    bundle.filaments.find_preset("PETG", false)->config.set_key_value("nozzle_temperature", new ConfigOptionInts{260});
    CHECK(F::invalidate_changed(project, bundle, signatures));
    CHECK_FALSE(project.heads[0].process);
    CHECK(project.heads[1].process.has_value());
    CHECK_THAT(F::refresh(bundle, project.heads[0]).process.temperature, WithinAbs(260., 1e-12));
    project.heads[0].process->temperature = 123.;
    CHECK_THAT(F::refresh(bundle, project.heads[0]).process.temperature, WithinAbs(260., 1e-12));
    bundle.printers.find_preset("Nozzle 1", false)->config.set_key_value("nozzle_diameter", new ConfigOptionFloats{.5});
    CHECK(F::invalidate_changed(project, bundle, signatures));
    CHECK_FALSE(project.heads[1].process);
    CHECK_THAT(F::refresh(bundle, project.heads[1]).process.nozzle_diameter, WithinAbs(.5, 1e-12));
    bundle.prints.get_edited_preset().config.set_key_value("outer_wall_speed", new ConfigOptionFloatsNullable{55.});
    CHECK(F::invalidate_changed(project, bundle, signatures));
    for (auto& head : project.heads)
        CHECK_FALSE(head.process);
    CHECK_THAT(F::refresh(bundle, project.heads[2]).process.speed, WithinAbs(55., 1e-12));
    bundle.filaments.find_preset("PETG-CF", false)->config.set_key_value("filament_shrink", new ConfigOptionPercents{98.});
    CHECK(F::invalidate_changed(project, bundle, signatures));
    const auto shrunk = F::refresh(bundle, project.heads[2]);
    CHECK_THAT(shrunk.process.shrink_compensation[0], WithinAbs(100. / 98., 1e-12));
    CHECK_THAT(shrunk.process.shrink_compensation[1], WithinAbs(100. / 98., 1e-12));
    bundle.prints.get_edited_preset().config.set_key_value("outer_wall_line_width", new ConfigOptionFloatOrPercent(125., true));
    CHECK(F::invalidate_changed(project, bundle, signatures));
    CHECK_THAT(F::refresh(bundle, project.heads[2]).process.line_width, WithinAbs(.75, 1e-12));
    bundle.filaments.find_preset("PETG-CF", false)->config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats{0.});
    CHECK_THROWS(F::refresh(bundle, project.heads[2]));
    CHECK_FALSE(project.heads[2].process);
}
TEST_CASE("Edited source presets and explicit nozzle slots are resolved without a slot-zero fallback", "[FormorixProfiles]")
{
    PresetBundle bundle;
    configure(bundle);
    S::Head head;
    head.nozzle_index = 2;
    bundle.printers.get_edited_preset().config.set_key_value("nozzle_diameter", new ConfigOptionFloats{.8, .4, .6});
    bundle.printers.get_edited_preset().config.set_key_value("extruder_variant_list",
                                                             new ConfigOptionStrings{"Direct Drive Standard", "Direct Drive Standard",
                                                                                     "Direct Drive Standard"});
    bundle.printers.get_edited_preset().config.set_key_value("printer_extruder_id", new ConfigOptionInts{1, 2, 3});
    bundle.printers.get_edited_preset().config.set_key_value("printer_extruder_variant",
                                                             new ConfigOptionStrings{"Direct Drive Standard", "Direct Drive Standard",
                                                                                     "Direct Drive Standard"});
    bundle.prints.get_edited_preset().config.set_key_value("print_extruder_id", new ConfigOptionInts{1, 2, 3});
    bundle.prints.get_edited_preset().config.set_key_value("print_extruder_variant",
                                                           new ConfigOptionStrings{"Direct Drive Standard", "Direct Drive Standard",
                                                                                   "Direct Drive Standard"});
    bundle.prints.get_edited_preset().config.set_key_value("outer_wall_speed", new ConfigOptionFloatsNullable{31., 52., 93.});
    bundle.filaments.get_edited_preset().config.set_key_value("nozzle_temperature", new ConfigOptionInts{231});
    auto resolved = F::refresh(bundle, head);
    CHECK_THAT(resolved.process.nozzle_diameter, WithinAbs(.6, 1e-12));
    CHECK_THAT(resolved.process.temperature, WithinAbs(231., 1e-12));
    CHECK_THAT(resolved.process.speed, WithinAbs(93., 1e-12));
    head.nozzle_index = 3;
    CHECK_THROWS(F::refresh(bundle, head));
    CHECK_FALSE(head.process);
    head.assign_nozzle("not installed", 0);
    CHECK_THROWS(F::refresh(bundle, head));
    CHECK_FALSE(head.process);
}
TEST_CASE("Assigned nozzle volume types select matching material variants and reject ambiguous inventories", "[FormorixProfiles]")
{
    PresetBundle bundle;
    configure(bundle);
    auto& nozzle = bundle.printers.find_preset("Nozzle 0", false)->config;
    nozzle.set_key_value("default_nozzle_volume_type", new ConfigOptionEnumsGeneric{nvtHighFlow});
    nozzle.set_key_value("extruder_variant_list", new ConfigOptionStrings{"Direct Drive Standard,Direct Drive High Flow"});
    nozzle.set_key_value("printer_extruder_id", new ConfigOptionInts{1, 1});
    nozzle.set_key_value("printer_extruder_variant", new ConfigOptionStrings{"Direct Drive Standard", "Direct Drive High Flow"});
    auto& material = bundle.filaments.find_preset("PETG", false)->config;
    material.set_key_value("filament_extruder_variant", new ConfigOptionStrings{"Direct Drive Standard", "Direct Drive High Flow"});
    material.set_key_value("nozzle_temperature", new ConfigOptionInts{230, 272});
    material.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats{10., 28.});
    auto project        = assignments();
    const auto resolved = F::refresh(bundle, project.heads[0]);
    CHECK_THAT(resolved.process.nozzle_diameter, WithinAbs(.8, 1e-12));
    CHECK_THAT(resolved.process.temperature, WithinAbs(272., 1e-12));
    CHECK_THAT(resolved.process.volumetric_limit, WithinAbs(28., 1e-12));
    CHECK(resolved.config.option<ConfigOptionInts>("filament_volume_map")->get_at(0) == nvtHighFlow);
    nozzle.set_key_value("extruder_nozzle_stats", new ConfigOptionStrings{"Standard#1|High Flow#1"});
    CHECK_THROWS(F::refresh(bundle, project.heads[0]));
    CHECK_FALSE(project.heads[0].process);
    nozzle.set_key_value("extruder_nozzle_stats", new ConfigOptionStrings{});
    material.set_key_value("filament_extruder_variant", new ConfigOptionStrings{"Direct Drive Standard"});
    CHECK_THROWS(F::refresh(bundle, project.heads[0]));
}
TEST_CASE("Legacy surface schemas migrate explicit nozzle bindings without trusting serialized snapshots", "[FormorixProfiles]")
{
    PresetBundle bundle;
    configure(bundle);
    auto project = assignments();
    for (auto& head : project.heads)
        F::refresh(bundle, head);
    auto data = nlohmann::json::parse(project.encode());
    for (int version : {1, 2}) {
        data["version"] = version;
        for (auto& head : data["heads"]) {
            head.erase("printer_preset");
            head.erase("nozzle_index");
        }
        std::string error;
        auto loaded = S::Project::decode(data.dump(), error);
        REQUIRE(loaded);
        CHECK(loaded->version == S::schema_version);
        CHECK(loaded->heads[0].printer_preset.empty());
        CHECK(loaded->heads[0].nozzle_index == 0);
        REQUIRE(loaded->heads[0].process);
        std::array<std::string, 3> fresh_signatures;
        CHECK(F::invalidate_changed(*loaded, bundle, fresh_signatures));
        for (const auto& head : loaded->heads)
            CHECK_FALSE(head.process);
    }
    project.heads[0].nozzle_index = 256;
    CHECK_THROWS(project.encode());
    for (const auto bad_slot : {nlohmann::json(-1), nlohmann::json(1.5), nlohmann::json(4294967296ULL)}) {
        data["heads"][0]["nozzle_index"] = bad_slot;
        std::string error;
        CHECK_FALSE(S::Project::decode(data.dump(), error));
    }
}
