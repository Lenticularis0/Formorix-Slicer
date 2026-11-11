#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixHybrid.hpp"
#include "libslic3r/FormorixGeometry.hpp"
#include "libslic3r/FormorixProcess.hpp"
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
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <utility>
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
    for (const auto& r : result.regions) {
        std::cout << "HYBRID " << name << "," << r.id << "," << std::setprecision(17) << result.partition.volume_residual << ","
                  << r.accuracy.bounds[2] << "," << r.coverage.sampled_maximum << "," << r.coverage.sampled_mean << ","
                  << r.coverage.sampled_rms << "," << r.coverage.uncovered_fraction << "," << r.coverage.outside_fraction << ","
                  << r.volume_ratio << "," << r.accuracy.verified() << "\n";
    }
}
} // namespace
TEST_CASE("Hybrid partition and compensated layers preserve a planar reference while exposing excess material", "[FormorixHybrid]")
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
    CHECK(r.volume_ratio > 1.1);
    CHECK_FALSE(r.accuracy.material_volume_plausible);
    CHECK_FALSE(r.accuracy.verified());
    CHECK(result.preview.timeline.valid());
    REQUIRE(result.preview.layers.size() == 2);
    CHECK_THAT(result.preview.layers.back().path[0].position.z(), WithinAbs(4.9, 1e-6));
    CHECK_THAT(result.preview.timeline.heads[0].back().to.position.z(), WithinAbs(5., 1e-6));
    for (size_t layer = 0; layer < result.preview.layers.size(); ++layer) {
        size_t count = 0;
        for (const auto& move : result.preview.timeline.heads[0])
            if (move.extrusion && move.layer == int(layer))
                ++count;
        CHECK(count + 1 == result.preview.layers[layer].path.size());
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
TEST_CASE("A vertical hybrid wall uses the same exact core boundary and explicit orientation allowance", "[FormorixHybrid]")
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
    REQUIRE(result.ok());
    CHECK(FG::valid_solid(result.core));
    CHECK_FALSE(result.regions[0].accuracy.verified());
    // The inclined ellipse has normal thickness about .298656 mm instead of
    // the .2 mm local layer height. This is a detected core/material conflict.
    CHECK_THAT(result.regions[0].nominal_core_overlap, WithinAbs(.098656, 2e-6));
    CHECK_FALSE(result.regions[0].accuracy.material_volume_plausible);
    CHECK(std::any_of(result.warnings.begin(), result.warnings.end(), [](const std::string& warning) {
        return warning.find("bead crosses the local core interface") != std::string::npos;
    }));
    metrics("vertical_wall", result);
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
TEST_CASE("Hybrid expansion bounds the final movement count across all layers", "[FormorixHybrid]")
{
    auto input = fixture(0);
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
    if (shape == 2) {
        REQUIRE(result.ok());
        CHECK(FG::valid_solid(result.core));
        metrics("complete_convex_top_boundary", result);
    } else {
        // The inward normal at these lateral boundaries points outside the
        // original solid. Clipping the shell alone cannot validate those paths.
        REQUIRE_FALSE(result.ok());
        CHECK(result.error.find("local path termination") != std::string::npos);
        CHECK(result.preview.layers.empty());
        std::cout << "HYBRID unsupported_complete_top," << shape << "," << result.error << "\n";
    }
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
    CHECK(result.error.find("overlaps another region") != std::string::npos);
    std::cout << "HYBRID native_curved_wall_with_side," << result.error << "\n";
    // The supplied editor demonstration also marks a perpendicular wall with
    // a different depth. Its intersecting skin is deliberately rejected above.
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
