#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixSurfaces.hpp"
#include "libslic3r/FormorixProcess.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Format/ProjectStrategy.hpp"
#include "test_utils.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <memory>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

using namespace Slic3r;
namespace FS = Slic3r::FormorixSurfaces;

namespace {
Model make_surface_model()
{
    Model model;
    auto *object = model.add_object();
    auto *volume = object->add_volume(make_cube(20., 20., 20.), ModelVolumeType::MODEL_PART, false);
    object->add_instance();
    TriangleSelector paint(volume->mesh());
    paint.set_facet(0, EnforcerBlockerType(1));
    paint.set_facet(1, EnforcerBlockerType(2));
    paint.set_facet(2, EnforcerBlockerType(32));
    volume->formorix_facets.set(paint);
    volume->formorix_topology = FS::topology_id(volume->mesh().its);
    FS::Project p;
    p.regions = {{1, "Roof"}, {2, "Side"}, {32, "Reinforcement"}};
    p.regions[2].settings.head_mask = 4;
    model.formorix_surfaces = p.encode();
    return model;
}

Model round_trip(Model &source)
{
    ScopedSlic3rTemporaryDir dir("formorix_surfaces");
    source.set_backup_path((dir.path() / "backup").string());
    auto config = DynamicPrintConfig::full_print_config();
    PlateData plate;
    plate.plate_index = 0;
    StoreParams params;
    params.path = (dir.path() / "surfaces.3mf").string();
    params.model = &source;
    params.config = &config;
    params.plate_data_list.push_back(&plate);
    params.strategy = SaveStrategy::Zip64 | SaveStrategy::Silence;
    REQUIRE(store_bbs_3mf(params));
    Model restored;
    DynamicPrintConfig restored_config;
    ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Disable);
    PlateDataPtrs plates;
    std::vector<Preset *> presets;
    bool bbl = false, orca = false;
    Semver version;
    const bool loaded = load_bbs_3mf(params.path.c_str(), &restored_config, &substitutions, &restored,
        &plates, &presets, &bbl, &orca, &version, nullptr, LoadStrategy::LoadModel | LoadStrategy::LoadConfig);
    release_PlateData_list(plates);
    for (auto *preset : presets) delete preset;
    REQUIRE(loaded);
    REQUIRE(restored.objects.size() == source.objects.size());
    return restored;
}
}

TEST_CASE("Per-head deposition profiles survive native project archives", "[FormorixSurfaces]")
{
    Model model = make_surface_model();
    std::string error;
    auto project = FS::Project::decode(model.formorix_surfaces, error);
    REQUIRE(project);
    for (int h = 0; h < 3; ++h) {
        project->heads[h].material_preset = "Orca material " + std::to_string(h);
        project->heads[h].printer_preset = "Orca nozzle source " + std::to_string(h);
        project->heads[h].nozzle_index = unsigned(h);
        project->heads[h].process_preset = "Orca process " + std::to_string(h);
        project->heads[h].local_width_limits = FormorixProcess::WidthLimits{.3 + .2 * h, .6 + .2 * h};
        Slic3r::FormorixProcess::Profile process;
        process.material_id = "material-" + std::to_string(h);
        process.nozzle_profile = "nozzle-" + std::to_string(h);
        process.nozzle_diameter = .4 + .2 * h;
        process.line_width = process.nozzle_diameter * 1.1;
        process.local_width_limits = project->heads[h].local_width_limits;
        process.print_flow_ratio = .95 + .02 * h;
        process.surface_flow_ratio = 1.01 + .01 * h;
        project->heads[h].process = process;
    }
    model.formorix_surfaces = project->encode();
    Model restored = round_trip(model);
    auto loaded = FS::Project::decode(restored.formorix_surfaces, error);
    REQUIRE(loaded);
    CHECK(loaded->encode() == project->encode());
}

TEST_CASE("Surface manufacturing wishes round trip independently of printer settings", "[FormorixSurfaces]")
{
    FS::Project p;
    p.regions = {{1, "Überhang / roof"}, {2, "Wall"}};
    auto &s = p.regions[0].settings;
    s.shell_mode = FS::ShellMode::FullWall;
    s.layers = 8; s.shell_thickness = 1.8; s.layer_height = .15;
    s.preserve_dimensions = false; s.use_infill = false; s.inherit_infill_first = false;
    s.dense_underlay = true; s.underlay_density = 75.; s.additional_support = true;
    s.support_material = FS::SupportMaterial::Soluble;
    s.allow_deviation = true; s.max_deviation = 12.; s.allow_vertical = true;
    s.direction = FS::Direction::Edge; s.edge = {0., 1., 2., 3., 4., 5.}; s.edge_selected = true;
    s.custom_direction = {1., 2., 3.}; s.alternate_directions = false;
    s.fine_final_layers = 3; s.fine_layer_height = .08; s.boundary_margin = .3; s.quality = 2; s.ironing = true;
    s.single_head = false; s.head_mask = 5;
    p.regions[0].visible = false; p.regions[0].continuous = true;
    p.heads[0] = {true, "PETG", FS::Build | FS::Surface};
    p.heads[1] = {false, "PVA", FS::Support};
    p.heads[2] = {true, "PETG-CF", FS::Reinforcement};
    p.strategy = FS::Strategy::MaterialRoles;
    p.selection = {2, 3.5, false, 50., 60., 100., .4};
    p.transitions = {{1, 2, FS::TransitionKind::Interwoven, 4., 60., FS::TransitionPattern::AlternatingLayers}};
    const std::string encoded = p.encode();
    std::string error;
    const auto restored = FS::Project::decode(encoded, error);
    REQUIRE(restored);
    CHECK(error.empty());
    CHECK(restored->encode() == encoded);
    CHECK(restored->regions[0].settings.head_mask == 5);
    CHECK(restored->heads[0].tasks == (FS::Build | FS::Surface));
    CHECK(FS::Project::decode("", error)->regions.empty());
}

TEST_CASE("Surface project input rejects unsupported schemas and invalid bounds", "[FormorixSurfaces]")
{
    FS::Project p;
    p.regions = {{1, "Roof"}, {2, "Wall"}};
    auto data = nlohmann::json::parse(p.encode());
    const auto invalid = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    switch (invalid) {
    case 0: data["version"] = FS::schema_version + 1; break;
    case 1: data["regions"][1]["id"] = 1; break;
    case 2: data["regions"][0]["settings"]["max_deviation"] = 15.1; break;
    case 3: data["regions"][0]["settings"]["head_mask"] = 3; break;
    case 4: data["regions"][0]["settings"]["layer_height"] = 0; break;
    case 5: data["heads"].erase(2); break;
    case 6: data["heads"][1]["tasks"] = 0; break;
    case 7: data["strategy"] = 5; break;
    case 8: data["regions"][0]["settings"]["custom_direction"] = {0., 0., 0.}; break;
    }
    std::string error;
    CHECK_FALSE(FS::Project::decode(data.dump(), error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("Declared head widths reject invalid ranges and clear only derived process snapshots", "[FormorixSurfaces]")
{
    FS::Project project;
    auto& head = project.heads[0];
    head.process = FormorixProcess::Profile{};
    head.process->material_id = "material";
    head.process->nozzle_profile = "nozzle";
    head.assign_width_limits(FormorixProcess::WidthLimits{.3, .6});
    CHECK_FALSE(head.process);
    head.assign_process("Existing Orca process");
    const auto saved = nlohmann::json::parse(project.encode());
    for (const auto& invalid : {nlohmann::json{{"minimum_mm", .7}, {"maximum_mm", .6}},
                               nlohmann::json{{"minimum_mm", 0.}, {"maximum_mm", .6}},
                               nlohmann::json{{"minimum_mm", .3}, {"maximum_mm", nullptr}},
                               nlohmann::json{{"minimum_mm", ".3"}, {"maximum_mm", .6}}}) {
        auto data = saved;
        data["heads"][0]["local_width_limits"] = invalid;
        std::string error;
        CHECK_FALSE(FS::Project::decode(data.dump(), error));
        CHECK_FALSE(error.empty());
    }
    CHECK_THROWS(head.assign_width_limits(FormorixProcess::WidthLimits{.3, std::numeric_limits<double>::infinity()}));
    REQUIRE(head.local_width_limits);
    CHECK_THAT(head.local_width_limits->maximum_mm, Catch::Matchers::WithinAbs(.6, 1e-12));
    head.assign_width_limits(std::nullopt);
    CHECK_FALSE(head.local_width_limits);
    CHECK(head.process_preset == "Existing Orca process");
}

TEST_CASE("Merging regions preserves paint ownership and records continuous path intent", "[FormorixSurfaces]")
{
    Model model = make_surface_model();
    auto *volume = model.objects[0]->volumes[0];
    TriangleSelector seam(volume->mesh()); seam.set_facet(4, EnforcerBlockerType::ENFORCER); volume->seam_facets.set(seam);
    const auto seam_before = volume->seam_facets.get_data();
    std::string error;
    auto p = *FS::Project::decode(model.formorix_surfaces, error);
    p.transitions = {{1, 2}, {2, 32, FS::TransitionKind::Parallel}};
    FS::merge_regions(p, model, {1, 2}, 1);
    REQUIRE(p.find(1)); CHECK(p.find(1)->continuous); CHECK(p.find(1)->settings.single_head);
    CHECK_FALSE(p.find(2)); REQUIRE(p.transitions.size() == 1);
    CHECK(p.transitions[0].first == 1); CHECK(p.transitions[0].second == 32);
    CHECK(p.transitions[0].kind == FS::TransitionKind::Parallel);
    TriangleSelector merged(volume->mesh()); merged.deserialize(volume->formorix_facets.get_data());
    CHECK(merged.num_facets(EnforcerBlockerType(1)) == 2);
    CHECK_FALSE(merged.has_facets(EnforcerBlockerType(2)));
    CHECK(merged.num_facets(EnforcerBlockerType(32)) == 1);
    CHECK(volume->seam_facets.get_data() == seam_before);
    FS::remove_regions(p, model, {1});
    CHECK(p.next_id() == 1); CHECK(p.transitions.empty());
    merged.deserialize(volume->formorix_facets.get_data());
    CHECK_FALSE(merged.has_facets(EnforcerBlockerType(1)));
    CHECK(merged.has_facets(EnforcerBlockerType(32)));
}

TEST_CASE("Subdivided surface brush marks and settings survive a 3MF round trip", "[FormorixSurfaces]")
{
    Model source = make_surface_model();
    auto *volume = source.objects[0]->volumes[0];
    TriangleSelector selector(volume->mesh());
    const auto &its = volume->mesh().its;
    const auto &t = its.indices[0];
    const Vec3f center = (its.vertices[t[0]] + its.vertices[t[1]] + its.vertices[t[2]]) / 3.f;
    const Vec3f normal = (its.vertices[t[1]] - its.vertices[t[0]]).cross(its.vertices[t[2]] - its.vertices[t[0]]).normalized();
    auto cursor = TriangleSelector::SinglePointCursor::cursor_factory(center, center + normal * 50.f, 2.f,
        TriangleSelector::SPHERE, Transform3d::Identity(), {});
    selector.select_patch(0, std::move(cursor), EnforcerBlockerType(32), Transform3d::Identity(), true);
    REQUIRE(selector.num_facets(EnforcerBlockerType(32)) > 1);
    volume->formorix_facets.set(selector);
    Model restored = round_trip(source);
    CHECK(restored.formorix_surfaces == source.formorix_surfaces);
    REQUIRE(restored.objects[0]->volumes.size() == 1);
    CHECK(restored.objects[0]->volumes[0]->formorix_facets.equals(volume->formorix_facets));
    CHECK(FS::painting_matches(*restored.objects[0]->volumes[0]));
    CHECK(restored.objects[0]->volumes[0]->mmu_segmentation_facets.empty());
}

TEST_CASE("Unknown surface schemas remain opaque through project save and load", "[FormorixSurfaces]")
{
    Model model = make_surface_model();
    model.formorix_surfaces = R"({"version":99,"future":{"preserve":"verbatim"}})";
    Model restored = round_trip(model);
    CHECK(restored.formorix_surfaces == model.formorix_surfaces);
    CHECK(restored.objects[0]->volumes[0]->formorix_facets.equals(model.objects[0]->volumes[0]->formorix_facets));
    std::string error;
    CHECK_FALSE(FS::Project::decode(restored.formorix_surfaces, error));
}

TEST_CASE("Topology replacement cannot reattach old region paint to different facets", "[FormorixSurfaces]")
{
    Model model = make_surface_model();
    auto *volume = model.objects[0]->volumes[0];
    volume->set_offset(Vec3d(5., 7., 9.));
    volume->set_scaling_factor(Vec3d(2., 1., 1.));
    CHECK(FS::painting_matches(*volume));
    auto replacement = volume->mesh();
    std::swap(replacement.its.indices[0], replacement.its.indices[5]);
    volume->set_mesh(std::move(replacement));
    CHECK_FALSE(FS::painting_matches(*volume));
    Model restored = round_trip(model);
    CHECK_FALSE(FS::painting_matches(*restored.objects[0]->volumes[0]));
    CHECK(restored.objects[0]->volumes[0]->formorix_facets.equals(volume->formorix_facets));
}

TEST_CASE("Model cloning preserves surface regions with independent annotation identities", "[FormorixSurfaces]")
{
    Model source = make_surface_model();
    Model clone;
    clone.assign_clone(source);
    CHECK(clone.formorix_surfaces == source.formorix_surfaces);
    const auto *a = source.objects[0]->volumes[0];
    const auto *b = clone.objects[0]->volumes[0];
    CHECK(a->formorix_facets.id() != b->formorix_facets.id());
    CHECK(a->formorix_facets.equals(b->formorix_facets));
    CHECK(FS::painting_matches(*b));
}
