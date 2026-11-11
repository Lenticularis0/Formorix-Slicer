#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixProfiles.hpp"
#include "libslic3r/FormorixSurfaces.hpp"
#include "libslic3r/FormorixProcess.hpp"
#include "libslic3r/FormorixDeposition.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <cmath>
#include <optional>
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
    for (int version : {1, 2, 3}) {
        auto legacy = data;
        legacy["version"] = version;
        for (auto& head : legacy["heads"]) {
            if (version < 3) {
                head.erase("printer_preset");
                head.erase("nozzle_index");
            }
            // An older schema cannot authorize a future adaptive width range.
            head["local_width_limits"] = {{"minimum_mm", .1}, {"maximum_mm", 2.}};
            head["process_preset"] = "untrusted future binding";
        }
        std::string error;
        auto loaded = S::Project::decode(legacy.dump(), error);
        REQUIRE(loaded);
        CHECK(loaded->version == S::schema_version);
        CHECK(loaded->heads[0].printer_preset == (version < 3 ? "" : "Nozzle 0"));
        CHECK(loaded->heads[0].nozzle_index == 0);
        CHECK(loaded->heads[0].process_preset.empty());
        CHECK_FALSE(loaded->heads[0].local_width_limits);
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

TEST_CASE("Surface width resolution follows Orca automatic percent and inherited width semantics", "[FormorixProfiles]")
{
    PresetBundle bundle;
    configure(bundle);
    auto project = assignments();
    auto& config = bundle.prints.get_edited_preset().config;
    config.set_key_value("outer_wall_line_width", new ConfigOptionFloatOrPercent(0., false));
    config.set_key_value("line_width", new ConfigOptionFloatOrPercent(0., false));
    for (auto& head : project.heads) {
        const auto automatic = F::refresh(bundle, head);
        const auto native = Flow::new_from_config_width(frExternalPerimeter, ConfigOptionFloatOrPercent(0., false),
                                                       float(automatic.process.nozzle_diameter), float(automatic.process.layer_height));
        CHECK_THAT(automatic.process.line_width, WithinAbs(native.width(), 1e-12));
        CHECK_FALSE(automatic.process.local_width_limits);
    }
    config.set_key_value("line_width", new ConfigOptionFloatOrPercent(135., true));
    CHECK_THAT(F::refresh(bundle, project.heads[2]).process.line_width, WithinAbs(.6 * 1.35, 1e-12));
    config.set_key_value("outer_wall_line_width", new ConfigOptionFloatOrPercent(.63, false));
    CHECK_THAT(F::refresh(bundle, project.heads[2]).process.line_width, WithinAbs(.63, 1e-12));
    config.set_key_value("outer_wall_line_width", new ConfigOptionFloatOrPercent(120., true));
    CHECK_THAT(F::refresh(bundle, project.heads[2]).process.line_width, WithinAbs(.72, 1e-12));
    CHECK(config.option<ConfigOptionFloatOrPercent>("outer_wall_line_width")->percent);
}

TEST_CASE("Three assigned Orca processes retain independent dimensions speeds and unchanged flow factors", "[FormorixProfiles]")
{
    PresetBundle bundle;
    configure(bundle);
    auto project = assignments();
    const std::array<double, 3> widths{.9, .42, .66}, heights{.3, .12, .2}, speeds{100., 22., 45.};
    const std::array<double, 3> print_flow{1.04, .97, 1.02}, surface_flow{.96, 1.03, .98};
    for (int h = 0; h < 3; ++h) {
        auto config = bundle.prints.get_edited_preset().config;
        config.set_key_value("outer_wall_line_width", new ConfigOptionFloatOrPercent(widths[h], false));
        config.set_key_value("layer_height", new ConfigOptionFloat(heights[h]));
        config.set_key_value("outer_wall_speed", new ConfigOptionFloatsNullable{speeds[h]});
        config.set_key_value("print_flow_ratio", new ConfigOptionFloat(print_flow[h]));
        config.set_key_value("set_other_flow_ratios", new ConfigOptionBool(true));
        config.set_key_value("outer_wall_flow_ratio", new ConfigOptionFloat(surface_flow[h]));
        // These are not the machine's bed-first or top-fill paths.
        config.set_key_value("first_layer_flow_ratio", new ConfigOptionFloat(1.7));
        config.set_key_value("top_solid_infill_flow_ratio", new ConfigOptionFloatsNullable{1.6});
        const std::string name = "Independent process " + std::to_string(h);
        bundle.prints.load_preset("", name, config, false);
        auto& head = project.heads[h];
        head.assign_process(name);
        head.assign_width_limits(FormorixProcess::WidthLimits{widths[h] - .05, widths[h] + .05});
        const auto resolved = F::refresh(bundle, head);
        const auto& p = resolved.process;
        CHECK_THAT(p.line_width, WithinAbs(widths[h], 1e-12));
        CHECK_THAT(p.layer_height, WithinAbs(heights[h], 1e-12));
        CHECK_THAT(p.speed, WithinAbs(speeds[h], 1e-12));
        CHECK_THAT(p.flow_ratio, WithinAbs(1. + .01 * h, 1e-12));
        CHECK_THAT(p.print_flow_ratio, WithinAbs(print_flow[h], 1e-12));
        CHECK_THAT(p.surface_flow_ratio, WithinAbs(surface_flow[h], 1e-12));
        CHECK_THAT(FormorixProcess::effective_surface_flow(p), WithinAbs((1. + .01 * h) * print_flow[h] * surface_flow[h], 1e-12));
        CHECK_THAT(resolved.config.option<ConfigOptionFloats>("filament_flow_ratio")->get_at(0), WithinAbs(p.flow_ratio, 1e-12));
        CHECK_THAT(resolved.config.opt_float("print_flow_ratio"), WithinAbs(print_flow[h], 1e-12));
        REQUIRE(p.local_width_limits);
        CHECK_THAT(p.local_width_limits->minimum_mm, WithinAbs(widths[h] - .05, 1e-12));
        const auto layer_override = F::resolve(F::source(bundle, head, .1));
        CHECK_THAT(layer_override.process.layer_height, WithinAbs(.1, 1e-12));
    }
    std::array<std::string, 3> signatures;
    F::invalidate_changed(project, bundle, signatures);
    for (auto& head : project.heads) F::refresh(bundle, head);
    auto& saved = bundle.prints.find_preset("Independent process 1", false)->config;
    saved.set_key_value("set_other_flow_ratios", new ConfigOptionBool(false));
    CHECK(F::invalidate_changed(project, bundle, signatures));
    CHECK(project.heads[0].process.has_value());
    CHECK_FALSE(project.heads[1].process);
    CHECK(project.heads[2].process.has_value());
    CHECK_THAT(F::refresh(bundle, project.heads[1]).process.surface_flow_ratio, WithinAbs(1., 1e-12));
    saved.set_key_value("outer_wall_line_width", new ConfigOptionFloatOrPercent(.8, false));
    CHECK_THROWS(F::refresh(bundle, project.heads[1]));
    CHECK_FALSE(project.heads[1].process);
    project.heads[1].assign_process("missing process");
    CHECK_THROWS(F::refresh(bundle, project.heads[1]));
}

TEST_CASE("Declared local width changes invalidate snapshots and never come from cached process data", "[FormorixProfiles]")
{
    PresetBundle bundle;
    configure(bundle);
    auto project = assignments();
    auto& head = project.heads[0];
    const auto original_signature = F::signature(F::source(bundle, head));
    F::refresh(bundle, head);
    head.assign_width_limits(FormorixProcess::WidthLimits{.8, 1.});
    CHECK_FALSE(head.process);
    CHECK(F::signature(F::source(bundle, head)) != original_signature);
    auto resolved = F::refresh(bundle, head);
    REQUIRE(resolved.process.local_width_limits);
    head.process->local_width_limits = FormorixProcess::WidthLimits{.01, 10.};
    resolved = F::refresh(bundle, head);
    CHECK_THAT(resolved.process.local_width_limits->minimum_mm, WithinAbs(.8, 1e-12));
    head.assign_width_limits(std::nullopt);
    CHECK_FALSE(head.process);
    CHECK_FALSE(F::refresh(bundle, head).process.local_width_limits);
    CHECK(F::signature(F::source(bundle, head)) == original_signature);
}
