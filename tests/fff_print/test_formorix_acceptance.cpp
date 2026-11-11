#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixCorePreview.hpp"
#include "libslic3r/FormorixGeometry.hpp"
#include "libslic3r/FormorixHybrid.hpp"
#include "libslic3r/FormorixProcess.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace Slic3r;
namespace FH = FormorixHybrid;
namespace FG = FormorixGeometry;
namespace CP = FormorixCorePreview;
using Catch::Matchers::WithinAbs;

TEST_CASE("A CAD shell near the sixteen hundred millimetre bed edge feeds its actual core to Orca", "[FormorixAcceptance][FormorixHybridCore]")
{
    const Vec3d origin(1594.625, 100.125, 2.25);
    constexpr double width = 5., top = 5.25, shell_depth = .25;
    const auto cube = make_cube(width, width, 3.);
    FH::Input input;
    input.solid.faces = cube.its.indices;
    for (const auto& vertex : cube.its.vertices) input.solid.vertices.push_back(vertex.cast<double>() + origin);
    const auto original = input.solid;
    std::array<CP::Source, 3> sources;
    for (int head = 0; head < 3; ++head) {
        auto& source = sources[head];
        source.config = DynamicPrintConfig::full_print_config();
        auto& p = source.process;
        p.material_id = std::array<const char*, 3>{"PETG-core", "PVA-support", "PETG-CF-skin"}[head];
        p.nozzle_profile = "acceptance-head-" + std::to_string(head);
        p.nozzle_diameter = std::array<double, 3>{.8, .4, .6}[head];
        p.line_width = head == 2 ? .65 : p.nozzle_diameter * 1.1;
        p.layer_height = head == 2 ? .125 : .25;
        if (head == 2) p.local_width_limits = FormorixProcess::WidthLimits{.6, .7};
        source.config.set_deserialize_strict({{"layer_height", std::to_string(p.layer_height)},
                                              {"initial_layer_print_height", std::to_string(p.layer_height)},
                                              {"nozzle_diameter", std::to_string(p.nozzle_diameter)},
                                              {"sparse_infill_density", "20%"}, {"enable_support", "0"},
                                              {"brim_type", "no_brim"}, {"skirt_loops", "0"},
                                              {"elefant_foot_compensation", "0"}, {"filament_flow_ratio", "1"},
                                              {"printable_area", "0x0,1600x0,1600x350,0x350"}});
        for (const char* key : {"line_width", "outer_wall_line_width", "inner_wall_line_width", "sparse_infill_line_width",
                                "internal_solid_infill_line_width", "top_surface_line_width"})
            source.config.set_deserialize_strict(key, std::to_string(p.line_width));
    }
    FH::RegionInput skin;
    skin.id = 1;
    skin.head = 2;
    skin.max_tilt = 75.;
    skin.process = sources[2].process;
    skin.surface.vertices = {{origin.x(), origin.y(), top}, {origin.x() + width, origin.y(), top},
                             {origin.x() + width, origin.y() + width, top}, {origin.x(), origin.y() + width, top}};
    skin.surface.faces = {{0, 1, 2}, {0, 2, 3}};
    skin.settings.head_mask = 4;
    skin.settings.layers = 2;
    skin.settings.layer_height = .125;
    skin.cad_reference = FG::plane({0., 0., top}, Vec3d::UnitZ());
    input.regions.push_back(skin);
    auto hybrid = FH::generate(input);
    INFO(hybrid.error);
    INFO(hybrid.material.error);
    REQUIRE(hybrid.ok());
    REQUIRE(hybrid.material.ok());
    REQUIRE(hybrid.regions.size() == 1);
    CHECK(hybrid.regions[0].accuracy.verified()); // selected nominal skin only, not conventional material or mechanics
    CHECK_THAT(FG::volume(hybrid.core), WithinAbs(width * width * (3. - shell_depth), 1e-8));
    CHECK_THAT(hybrid.partition.volume_residual, WithinAbs(0., 1e-8));
    const auto native = CP::slice(hybrid.core, sources, 0, -1);
    INFO(native.error);
    REQUIRE(native.ok());
    CHECK(native.float_conversion_bound < .0001);
    CHECK(native.coordinate_quantization_bound < .00001);
    CHECK_THAT(native.local_origin.x(), WithinAbs(1597.125, 1e-12));
    auto composed_skin_accuracy = hybrid.regions[0].accuracy;
    const double native_conversion = FH::add_error_bounds(
        std::max(native.float_conversion_bound, native.modifier_float_conversion_bound),
        FH::add_error_bounds(native.coordinate_quantization_bound, native.native_slice_expansion_bound));
    composed_skin_accuracy.bounds[2] = FH::add_error_bounds(composed_skin_accuracy.bounds[2], native_conversion);
    CHECK(composed_skin_accuracy.total_bound() >= hybrid.regions[0].accuracy.total_bound());
    CHECK(composed_skin_accuracy.verified());
    bool wall = false, infill = false;
    double min_x = std::numeric_limits<double>::infinity(), max_x = -min_x, max_z = -min_x;
    std::vector<FH::UnderlaySegment> deposited;
    for (const auto& path : native.paths) {
        CHECK(path.head == 0);
        CHECK(path.process.material_id == "PETG-core");
        CHECK_THAT(path.process.nozzle_diameter, WithinAbs(.8, 0.));
        wall |= is_perimeter(path.role);
        infill |= is_infill(path.role);
        for (const auto& point : path.points) {
            min_x = std::min(min_x, point.x());
            max_x = std::max(max_x, point.x());
            max_z = std::max(max_z, point.z());
            CHECK(point.x() >= origin.x() - native.coordinate_quantization_bound);
            CHECK(point.x() <= origin.x() + width + native.coordinate_quantization_bound);
            CHECK(point.z() <= top - shell_depth + native.float_conversion_bound + native.coordinate_quantization_bound);
        }
        if (path.nominal_supplied_mm3_per_mm > 0.)
            for (std::size_t i = 1; i < path.points.size(); ++i)
                deposited.push_back({path.points[i - 1], path.points[i], path.width, path.height, path.head, path.process.material_id,
                                     path.support, path.role == erInternalInfill, is_bridge(path.role), path.flow_geometry_unverified,
                                     path.dense_transition});
    }
    CHECK(wall);
    CHECK(infill);
    CHECK(max_x - min_x > 4.);
    CHECK_THAT(max_z, WithinAbs(top - shell_depth, 1e-8));
    REQUIRE_FALSE(hybrid.preview.timeline.heads[2].empty());
    for (const auto& motion : hybrid.preview.timeline.heads[2])
        if (motion.extrusion) {
            REQUIRE(motion.process);
            CHECK(motion.process->material_id == "PETG-CF-skin");
            CHECK_THAT(motion.process->nozzle_diameter, WithinAbs(.6, 0.));
        }
    CHECK_FALSE(hybrid.preview.timeline.sample(0, 0.).pose_known);
    CHECK_FALSE(hybrid.preview.timeline.sample(1, 0.).pose_known);
    FH::audit_underlay(hybrid, deposited);
    CHECK(hybrid.regions[0].underlay.samples > 0);
    CHECK_FALSE(hybrid.regions[0].underlay.coverage_bounded);
    CHECK(input.solid.vertices == original.vertices);
    CHECK(input.solid.faces == original.faces);
    std::cout << std::setprecision(17) << "ACCEPTANCE_NATIVE_1600,min_x=" << min_x << ",max_x=" << max_x << ",max_z=" << max_z
              << ",core_float_bound=" << native.float_conversion_bound << ",native_grid_bound=" << native.coordinate_quantization_bound
              << ",core_volume_residual=" << native.converted_volume_residual << ",shell_error_bounds=";
    for (double bound : hybrid.regions[0].accuracy.bounds) std::cout << bound << '/';
    std::cout << ",native_added_bound=" << native_conversion << ",composed_skin_total_bound=" << composed_skin_accuracy.total_bound()
              << ",composed_skin_verified=" << composed_skin_accuracy.verified() << ",whole_hybrid_verified=0"
              << ",material_maximum=" << hybrid.material.boundary_distance.maximum
              << ",material_mean=" << hybrid.material.boundary_distance.mean << ",material_rms=" << hybrid.material.boundary_distance.rms
              << ",underlay_samples=" << hybrid.regions[0].underlay.samples << ",underlay_missing=" << hybrid.regions[0].underlay.missing << '\n';
}
