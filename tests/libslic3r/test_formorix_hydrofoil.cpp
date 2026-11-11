#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixGeometry.hpp"
#include "libslic3r/FormorixSurfacePaths.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Format/ProjectStrategy.hpp"
#include "test_utils.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
namespace FG = FormorixGeometry;
namespace SP = FormorixSurfacePaths;
using Catch::Matchers::WithinAbs;

namespace {
std::string file_bytes(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
Vec3d face_normal(const FG::Mesh& mesh, const Vec3i32& f)
{
    return (mesh.vertices[f[1]] - mesh.vertices[f[0]]).cross(mesh.vertices[f[2]] - mesh.vertices[f[0]]);
}
Vec3d interpolate(const FG::Mesh& mesh, int face, const std::array<double, 3>& barycentric)
{
    const auto& f = mesh.faces[face];
    return barycentric[0] * mesh.vertices[f[0]] + barycentric[1] * mesh.vertices[f[1]] + barycentric[2] * mesh.vertices[f[2]];
}
Vec3d interpolate_normal(const SP::Input& input, int face, const std::array<double, 3>& barycentric)
{
    const auto& f = input.surface.faces[face];
    return (barycentric[0] * input.vertex_normals[f[0]] + barycentric[1] * input.vertex_normals[f[1]] +
            barycentric[2] * input.vertex_normals[f[2]]).normalized();
}
} // namespace

TEST_CASE("The original hydrofoil negative retains its returning surface in bounded three dimensional contours", "[FormorixHydrofoil]")
{
    const std::string path = std::string(TEST_DATA_DIR) + "/formorix/hydrofoil-negative-original.3mf";
    const auto bytes = file_bytes(path);
    REQUIRE(bytes.size() == 301886);
    ScopedSlic3rTemporaryDir temporary("formorix-original-hydrofoil");
    DynamicPrintConfig config;
    ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Disable);
    Model original = Model::read_from_file(path, &config, &substitutions,
                                          LoadStrategy::LoadModel | LoadStrategy::LoadConfig | LoadStrategy::AddDefaultInstances);
    REQUIRE(original.objects.size() == 1);
    REQUIRE(original.objects.front()->volumes.size() == 1);
    REQUIRE(original.objects.front()->instances.size() == 1);
    const auto* source_volume = original.objects.front()->volumes.front();
    const auto source_vertices = source_volume->mesh().its.vertices;
    const auto source_faces = source_volume->mesh().its.indices;
    const auto source_volume_matrix = source_volume->get_matrix();
    const auto source_instance_matrix = original.objects.front()->instances.front()->get_matrix();
    REQUIRE(source_vertices.size() == 11709);
    REQUIRE(source_faces.size() == 23414);
    FG::Mesh source;
    source.faces = source_faces;
    // The 3MF has an identity build transform, but the native importer centers
    // its mesh and transfers a single volume's offset into the instance.
    // Reconstruct file coordinates with BOTH native transforms.
    const Transform3d source_transform = source_instance_matrix * source_volume_matrix;
    CHECK(source_transform.linear().isApprox(Transform3d::Identity().linear(), 1e-12));
    CHECK_THAT((source_transform.translation() - source_volume->source.mesh_offset).norm(), WithinAbs(0., 1e-9));
    std::cout << std::setprecision(17) << "HYDROFOIL_TRANSFORM,volume_offset=" << source_volume_matrix.translation().transpose()
              << ",instance_offset=" << source_instance_matrix.translation().transpose()
              << ",source_mesh_offset=" << source_volume->source.mesh_offset.transpose() << "\n";
    for (const auto& p : source_vertices)
        source.vertices.push_back(source_transform * p.cast<double>());

    const auto source_atlas = FG::make_atlas(source);
    REQUIRE(source_atlas.boundary_edges == 0);
    std::vector<Vec3d> normals;
    for (const auto& face : source.faces) {
        const Vec3d n = face_normal(source, face);
        REQUIRE(n.norm() > 0.);
        normals.push_back(n.normalized());
    }
    // Identify the original CAD-export face by geometry, not by loader face order.
    const Vec3d seed_reference(49.9004, 15.385033333333332, 13.273866666666668);
    std::size_t seed = 0;
    double seed_distance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < source.faces.size(); ++i) {
        const auto& f = source.faces[i];
        const Vec3d center = (source.vertices[f[0]] + source.vertices[f[1]] + source.vertices[f[2]]) / 3.;
        const double distance = (center - seed_reference).norm();
        if (distance < seed_distance) {
            seed = i;
            seed_distance = distance;
        }
    }
    REQUIRE(seed_distance < .001); // native float import, not an exact CAD-reference claim
    std::vector<std::vector<int>> adjacent(source.faces.size());
    for (const auto& portal : source_atlas.portals) {
        adjacent[portal.first_face].push_back(portal.second_face);
        adjacent[portal.second_face].push_back(portal.first_face);
    }
    const double threshold = std::cos(std::acos(-1.) / 4.);
    std::vector<bool> selected(source.faces.size(), false);
    std::vector<int> queue{int(seed)};
    selected[seed] = true;
    for (std::size_t i = 0; i < queue.size(); ++i)
        for (int neighbor : adjacent[queue[i]])
            if (!selected[neighbor] && normals[queue[i]].dot(normals[neighbor]) >= threshold) {
                selected[neighbor] = true;
                queue.push_back(neighbor);
            }
    std::sort(queue.begin(), queue.end());
    REQUIRE(queue.size() == 21196);

    // Change only a separate instance. The imported mesh and original file stay unchanged.
    Model placed(original);
    Transform3d requested = Transform3d::Identity();
    requested.linear() << 0., 1., 0., 1., 0., 0., 0., 0., -1.;
    requested.translation() = Vec3d(327.3, 35.0111, 29.003);
    Geometry::Transformation placement;
    placement.set_matrix(requested * source_instance_matrix);
    placed.objects.front()->instances.front()->set_transformation(placement);
    const auto transform = placed.objects.front()->instances.front()->get_matrix();
    CHECK_THAT(transform.linear().determinant(), WithinAbs(1., 1e-12));
    CHECK(transform.matrix().isApprox((requested * source_instance_matrix).matrix(), 1e-12));
    SP::Input input;
    std::map<int, int> vertex_ids;
    for (int source_face : queue) {
        Vec3i32 face;
        for (int k = 0; k < 3; ++k) {
            const int source_vertex = source.faces[source_face][k];
            auto [it, inserted] = vertex_ids.emplace(source_vertex, int(input.surface.vertices.size()));
            if (inserted)
                input.surface.vertices.push_back(transform * (source_volume_matrix * source_vertices[source_vertex].cast<double>()));
            face[k] = it->second;
        }
        input.surface.faces.push_back(face);
    }
    input.vertex_normals.assign(input.surface.vertices.size(), Vec3d::Zero());
    std::size_t returning_faces = 0;
    for (const auto& face : input.surface.faces) {
        const auto n = face_normal(input.surface, face);
        returning_faces += n.z() < 0.;
        for (int k = 0; k < 3; ++k)
            input.vertex_normals[face[k]] += n;
    }
    std::size_t returning_normals = 0;
    double maximum_tilt = 0.;
    for (auto& n : input.vertex_normals) {
        REQUIRE(n.norm() > 0.);
        n.normalize();
        returning_normals += n.z() < 0.;
        maximum_tilt = std::max(maximum_tilt, std::acos(std::clamp(n.z(), -1., 1.)) * 180. / std::acos(-1.));
    }
    const auto atlas = FG::make_atlas(input.surface);
    std::cout << std::setprecision(17) << "HYDROFOIL_IMPORT,source_vertices=" << source.vertices.size()
              << ",source_faces=" << source.faces.size() << ",seed_face=" << seed << ",seed_distance_mm=" << seed_distance
              << ",selected_vertices=" << input.surface.vertices.size() << ",selected_faces=" << input.surface.faces.size()
              << ",boundary_edges=" << atlas.boundary_edges << ",returning_faces=" << returning_faces
              << ",returning_vertex_normals=" << returning_normals << ",max_tilt_deg=" << maximum_tilt
              << ",cad_reference=unknown,whole_surface_accuracy=unverified\n";
    REQUIRE(input.surface.vertices.size() == 10696);
    CHECK(atlas.boundary_edges == 194);
    CHECK(input.surface.vertices.size() + input.surface.faces.size() == atlas.portals.size() + atlas.boundary_edges + 1);
    CHECK(returning_faces == 200);
    CHECK(returning_normals == 147);
    CHECK(maximum_tilt > 107.);

    const auto before = input.surface;
    SP::Options options;
    options.plane_normal = Vec3d::UnitX();
    options.plane_offsets = {10., 340., 670.};
    options.max_faces = 25000;
    options.max_planes = 3;
    options.max_knots = 100000;
    options.max_carrier_candidates = 500000;
    options.max_segment_length = 2.;
    options.normal_offset_distance = .6;
    options.interpolation_tolerance = .001;
    const auto result = SP::trace(input, options);
    INFO(result.reason);
    std::cout << "HYDROFOIL_CONTOURS,status=" << int(result.status) << ",runs=" << result.runs.size()
              << ",carrier_candidates=" << result.carrier_candidates << ",position_rounding_mm=" << result.position_rounding_bound
              << ",normal_rounding=" << result.normal_rounding_bound << ",offset_interpolation_mm=" << result.offset_interpolation_bound
              << ",bounded=" << result.interpolation_bounded << ",reason=" << result.reason << "\n";
    CHECK(input.surface.vertices == before.vertices);
    CHECK(input.surface.faces == before.faces);
    CHECK(source_volume->mesh().its.vertices == source_vertices);
    CHECK(source_volume->mesh().its.indices == source_faces);
    CHECK(source_volume->get_matrix().matrix() == source_volume_matrix.matrix());
    CHECK(original.objects.front()->instances.front()->get_matrix().matrix() == source_instance_matrix.matrix());
    CHECK(file_bytes(path) == bytes);
    REQUIRE(result.ok());
    REQUIRE(result.interpolation_bounded);
    CHECK(result.offset_interpolation_bound <= options.interpolation_tolerance);
    CHECK(result.carrier_candidates <= options.max_carrier_candidates);
    CHECK(result.position_rounding_bound < 1e-8);
    std::array<std::size_t, 3> runs_per_plane{};
    std::size_t segments = 0, negative_carriers = 0;
    double carrier_error = 0., normal_error = 0., offset_error = 0.;
    for (const auto& run : result.runs) {
        REQUIRE(run.plane_index < runs_per_plane.size());
        ++runs_per_plane[run.plane_index];
        REQUIRE(run.knots.size() >= 2);
        REQUIRE(run.carriers.size() == run.knots.size() - (run.closed ? 0u : 1u));
        for (std::size_t i = 0; i < run.carriers.size(); ++i) {
            const auto& carrier = run.carriers[i];
            REQUIRE(carrier.face >= 0);
            REQUIRE(std::size_t(carrier.face) < input.surface.faces.size());
            const auto& from = run.knots[i];
            const auto& to = run.knots[(i + 1) % run.knots.size()];
            ++segments;
            negative_carriers += face_normal(input.surface, input.surface.faces[carrier.face]).z() < 0.;
            CHECK((to.position - from.position).norm() > 0.);
            CHECK((to.position - from.position).norm() <= options.max_segment_length + 2. * result.position_rounding_bound + 1e-10);
            for (const auto* barycentric : {&carrier.from_barycentric, &carrier.to_barycentric}) {
                CHECK_THAT((*barycentric)[0] + (*barycentric)[1] + (*barycentric)[2], WithinAbs(1., 1e-10));
                for (double b : *barycentric) {
                    CHECK(b >= -1e-10);
                    CHECK(b <= 1. + 1e-10);
                }
            }
            carrier_error = std::max({carrier_error,
                (from.position - interpolate(input.surface, carrier.face, carrier.from_barycentric)).norm(),
                (to.position - interpolate(input.surface, carrier.face, carrier.to_barycentric)).norm()});
            normal_error = std::max({normal_error,
                (from.normal - interpolate_normal(input, carrier.face, carrier.from_barycentric)).norm(),
                (to.normal - interpolate_normal(input, carrier.face, carrier.to_barycentric)).norm()});
            CHECK_THAT(from.normal.norm(), WithinAbs(1., 1e-10));
            CHECK_THAT(from.position.x(), WithinAbs(options.plane_offsets[run.plane_index], 1e-9));
            for (double fraction : {.25, .5, .75}) {
                std::array<double, 3> barycentric;
                for (int k = 0; k < 3; ++k)
                    barycentric[k] = (1. - fraction) * carrier.from_barycentric[k] + fraction * carrier.to_barycentric[k];
                const Vec3d exact_field = interpolate(input.surface, carrier.face, barycentric) -
                    options.normal_offset_distance * interpolate_normal(input, carrier.face, barycentric);
                const Vec3d interpolated = (1. - fraction) * (from.position - options.normal_offset_distance * from.normal) +
                    fraction * (to.position - options.normal_offset_distance * to.normal);
                offset_error = std::max(offset_error, (exact_field - interpolated).norm());
            }
        }
    }
    for (std::size_t runs : runs_per_plane)
        CHECK(runs == 1); // no artificial restart at triangle or original-surface seams
    CHECK(negative_carriers > 0);
    CHECK(carrier_error <= 8. * result.position_rounding_bound + 1e-9);
    CHECK(normal_error <= 8. * result.normal_rounding_bound + 1e-9);
    CHECK(offset_error <= result.offset_interpolation_bound + 1e-9);
    FG::AccuracyBudget evidence;
    evidence.upper_bounds[2] = result.offset_interpolation_bound;
    CHECK_FALSE(evidence.nominal_verified(.01)); // no known CAD reference or surface-wide material coverage
    std::cout << "HYDROFOIL_CARRIERS,segments=" << segments << ",returning_segments=" << negative_carriers
              << ",carrier_error_mm=" << carrier_error << ",normal_error=" << normal_error
              << ",sampled_offset_error_mm=" << offset_error << ",cad_reference=unknown,whole_surface_accuracy=unverified\n";
}
