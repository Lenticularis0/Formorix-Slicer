#include <catch2/catch_test_macros.hpp>

#include "libslic3r/Formorix5D.hpp"
#include "libslic3r/FormorixSurfaces.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/GCode.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "test_helpers.hpp"
#include "test_utils.hpp"

#include <boost/filesystem/operations.hpp>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace Slic3r;

namespace {
std::string commands(const std::string &gcode)
{
    std::istringstream stream(gcode);
    std::string line, result;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.front() != ';') result += line.substr(0, line.find(';')) + '\n';
    }
    return result;
}
}

TEST_CASE("Experimental Formorix jobs are blocked before slicing or writing output", "[Formorix5D]")
{
    auto config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict("formorix_5d_enabled", "1");
    Print print;
    Model model;
    Slic3r::Test::init_print({Slic3r::Test::cube(10.)}, print, model, config);
    CHECK_FALSE(print.validate().string.empty());
    CHECK_THROWS_AS(print.process(), std::runtime_error);
    ScopedTemporaryFile file(".gcode");
    CHECK_THROWS_AS(print.export_gcode(file.string(), nullptr), std::runtime_error);
    CHECK_THROWS_AS(print.export_gcode_from_previous_file(file.string(), nullptr, {}), std::runtime_error);
    GCode generator;
    CHECK_THROWS_AS(generator.do_export(&print, file.string().c_str(), nullptr, {}), std::runtime_error);
    CHECK_FALSE(boost::filesystem::exists(file.path()));
}

TEST_CASE("Disabled Formorix settings leave conventional motion commands unchanged", "[Formorix5D]")
{
    auto config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict("layer_height", "0.2");
    config.set_deserialize_strict("initial_layer_print_height", "0.2");
    const auto baseline = Slic3r::Test::slice({Slic3r::Test::cube(10.)}, config);
    config.set_deserialize_strict("formorix_5d_head_2_a", "50");
    config.set_deserialize_strict("formorix_5d_target_speed", "900");
    const auto modified = Slic3r::Test::slice({Slic3r::Test::cube(10.)}, config);
    REQUIRE_FALSE(commands(baseline).empty());
    CHECK(commands(baseline) == commands(modified));
}

TEST_CASE("Surface GUI annotations do not enter conventional toolpaths or bypass the experimental guard", "[FormorixSurfaces]")
{
    auto config = DynamicPrintConfig::full_print_config();
    // init_print enables comments; keep the configuration identical on reapply.
    config.set_deserialize_strict("gcode_comments", "1");
    config.set_deserialize_strict("layer_height", "0.2");
    config.set_deserialize_strict("initial_layer_print_height", "0.2");
    Print print;
    Model model;
    Slic3r::Test::init_print({Slic3r::Test::cube(10.)}, print, model, config);
    const auto baseline = commands(Slic3r::Test::gcode(print));
    FormorixSurfaces::Project data;
    data.regions = {{1, "5D GUI test"}};
    data.regions[0].continuous = true;
    data.regions[0].settings.shell_mode = FormorixSurfaces::ShellMode::FullWall;
    data.strategy = FormorixSurfaces::Strategy::HighSpeed;
    model.formorix_surfaces = data.encode();
    auto *volume = model.objects[0]->volumes[0];
    TriangleSelector selector(volume->mesh());
    selector.set_facet(0, EnforcerBlockerType::ENFORCER);
    volume->formorix_facets.set(selector);
    volume->formorix_topology = FormorixSurfaces::topology_id(volume->mesh().its);
    print.apply(model, config);
    REQUIRE_FALSE(baseline.empty());
    CHECK(commands(Slic3r::Test::gcode(print)) == baseline);
    config.set_deserialize_strict("formorix_5d_enabled", "1");
    print.apply(model, config);
    CHECK_THROWS_AS(print.process(), std::runtime_error);
    ScopedTemporaryFile output(".gcode");
    CHECK_THROWS_AS(print.export_gcode(output.string(), nullptr), std::runtime_error);
    CHECK_FALSE(boost::filesystem::exists(output.path()));
}

TEST_CASE("Switching to Formorix invalidates a previously exported conventional job", "[Formorix5D]")
{
    auto config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict("layer_height", "0.2");
    config.set_deserialize_strict("initial_layer_print_height", "0.2");
    Print print;
    Model model;
    Slic3r::Test::init_print({Slic3r::Test::cube(10.)}, print, model, config);
    REQUIRE_FALSE(Slic3r::Test::gcode(print).empty());
    REQUIRE(print.is_step_done(psGCodeExport));

    config.set_deserialize_strict("formorix_5d_enabled", "1");
    print.apply(model, config);
    CHECK_FALSE(print.is_step_done(psGCodeExport));
    ScopedTemporaryFile file(".gcode");
    CHECK_THROWS_AS(print.export_gcode_from_previous_file(file.string(), nullptr, {}), std::runtime_error);
    CHECK_FALSE(boost::filesystem::exists(file.path()));

    config.set_deserialize_strict("formorix_5d_enabled", "0");
    print.apply(model, config);
    CHECK_FALSE(Slic3r::Test::gcode(print).empty());
}
