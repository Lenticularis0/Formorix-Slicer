#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixGeometry.hpp"
#include "libslic3r/FormorixHybrid.hpp"
#include "libslic3r/FormorixMaterial.hpp"
#include "libslic3r/FormorixProcess.hpp"
#include "libslic3r/FormorixSurfaces.hpp"
#include "libslic3r/Point.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
namespace FH = FormorixHybrid;
namespace FG = FormorixGeometry;
namespace FM = FormorixMaterial;
namespace FS = FormorixSurfaces;
using Catch::Matchers::WithinAbs;

namespace {
// Conforming complete top and closed base, including both chord endpoints.
// The selected top is never cropped to hide a steep leading/trailing boundary.
FH::Input top_body(const std::vector<double>& xs, const std::vector<double>& ys,
                   const std::function<double(double, double)>& height)
{
    FH::Input input;
    const int nx = int(xs.size()), ny = int(ys.size());
    for (double y : ys)
        for (double x : xs)
            input.solid.vertices.emplace_back(x, y, height(x, y));
    for (int j = 0; j + 1 < ny; ++j)
        for (int i = 0; i + 1 < nx; ++i) {
            const int a = j * nx + i, b = a + 1, c = a + nx, d = c + 1;
            input.solid.faces.emplace_back(a, b, d);
            input.solid.faces.emplace_back(a, d, c);
        }
    FH::RegionInput region;
    region.id = 1;
    region.surface = input.solid;
    region.process.material_id = "acceptance-PETG";
    region.process.nozzle_profile = "acceptance-0.8";
    region.process.nozzle_diameter = .8;
    region.process.line_width = .55;
    region.process.layer_height = .1;
    region.settings.layers = 2;
    region.settings.layer_height = .1;
    region.max_tilt = 75.;
    input.regions.push_back(std::move(region));
    std::vector<int> ring;
    for (int i = 0; i < nx; ++i) ring.push_back(i);
    for (int j = 1; j < ny; ++j) ring.push_back(j * nx + nx - 1);
    for (int i = nx - 2; i >= 0; --i) ring.push_back((ny - 1) * nx + i);
    for (int j = ny - 2; j > 0; --j) ring.push_back(j * nx);
    const int lower = int(input.solid.vertices.size());
    for (int i : ring)
        input.solid.vertices.emplace_back(input.solid.vertices[i].x(), input.solid.vertices[i].y(), 0.);
    const int center = int(input.solid.vertices.size());
    input.solid.vertices.emplace_back((xs.front() + xs.back()) * .5, (ys.front() + ys.back()) * .5, 0.);
    for (int i = 0; i < int(ring.size()); ++i) {
        const int j = (i + 1) % int(ring.size());
        input.solid.faces.emplace_back(ring[i], lower + i, lower + j);
        input.solid.faces.emplace_back(ring[i], lower + j, ring[j]);
        input.solid.faces.emplace_back(center, lower + j, lower + i);
    }
    // This is a declared test resource ceiling, not a geometric failure reason.
    input.material_options.max_elapsed_seconds = 10.;
    return input;
}

void report(const char* name, const FH::Result& result)
{
    const auto measured = [&](double value) { return result.material.ok() ? value : std::numeric_limits<double>::quiet_NaN(); };
    std::cout << std::setprecision(17) << "ACCEPTANCE_GEOMETRY," << name << ",ok=" << result.ok()
              << ",error=" << result.error << ",material_status=" << int(result.material.status)
              << ",material_error=" << result.material.error << ",material_seconds=" << result.material.elapsed_seconds
              << ",missing_mm3=" << measured(result.material.missing_volume) << ",outside_mm3=" << measured(result.material.outside_volume)
              << ",duplicate_mm3=" << measured(result.material.duplicate_volume) << ",core_overlap_mm3=" << measured(result.material.core_overlap_volume)
              << ",maximum_mm=" << measured(result.material.boundary_distance.maximum)
              << ",mean_mm=" << measured(result.material.boundary_distance.mean) << ",rms_mm=" << measured(result.material.boundary_distance.rms)
              << ",upper_mm=" << measured(result.material.boundary_distance.upper_bound) << '\n';
    for (const auto& region : result.regions) {
        std::cout << "ACCEPTANCE_REGION," << name << ",region=" << region.id << ",verified=" << region.accuracy.verified()
                  << ",whole_surface=" << region.accuracy.whole_surface_covered
                  << ",material_partition=" << region.accuracy.material_partition_verified
                  << ",material_plausible=" << region.accuracy.material_volume_plausible << ",bounds=";
        for (double bound : region.accuracy.bounds) std::cout << bound << '/';
        std::cout << ",total_bound=" << region.accuracy.total_bound() << '\n';
    }
}

void no_partial_plan(const FH::Result& result)
{
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.error.empty());
    CHECK(result.preview.layers.empty());
    CHECK(result.material_runs.empty());
    CHECK(result.regions.empty());
    CHECK(result.core.faces.empty());
    for (int head = 0; head < 3; ++head) {
        CHECK(result.preview.timeline.heads[head].empty());
        CHECK_FALSE(result.preview.timeline.sample(head, 0.).pose_known);
    }
}
} // namespace

TEST_CASE("The complete NACA negative mould keeps its nose and tail under the real tilt limit", "[FormorixAcceptance]")
{
    // Closed NACA 0012 upper side, chord 100 mm. t=sqrt(x/chord) removes
    // the square-root singularity; the derivative bound covers the entire curve.
    const FG::Curve curve{
        [](double t) -> Vec3d {
            const double u = t * t;
            const double h = 60. * (.2969 * t - .126 * u - .3516 * u * u + .2843 * u * u * u - .1036 * u * u * u * u);
            return {100. * u, 0., 10. - h};
        },
        [](double, double b) {
            return std::hypot(200., 60. * (.252 + 12. * .3516 * b * b + 30. * .2843 * std::pow(b, 4) + 56. * .1036 * std::pow(b, 6)));
        }};
    const auto discretized = FG::discretize(curve, 0., 1., .002);
    REQUIRE(discretized.bounded);
    REQUIRE(discretized.samples.size() > 16);
    std::vector<double> xs;
    for (const auto& sample : discretized.samples) xs.push_back(sample.point.x());
    auto input = top_body(xs, {0., 1., 2.}, [](double x, double) {
        const double u = x / 100.;
        if (x == 0. || x == 100.) return 10.; // exact analytical closed endpoints
        return 10. - 60. * (.2969 * std::sqrt(u) - .126 * u - .3516 * u * u + .2843 * u * u * u - .1036 * u * u * u * u);
    });
    auto& region = input.regions.front();
    region.process.nozzle_profile = "acceptance-0.4";
    region.process.nozzle_diameter = .4;
    region.process.line_width = .45;
    region.process.layer_height = .1;
    region.settings.layers = 1;
    region.settings.layer_height = region.process.layer_height;
    region.settings.direction = FS::Direction::Custom;
    region.settings.custom_direction = {1., 0., 0.};
    region.settings.allow_vertical = true;
    REQUIRE(FG::valid_solid(input.solid));
    CHECK_THAT(xs.front(), WithinAbs(0., 0.));
    CHECK_THAT(xs.back(), WithinAbs(100., 0.));
    CHECK_THAT(region.max_tilt, WithinAbs(75., 0.));
    CHECK_FALSE(region.settings.allow_deviation);
    double steepest = 0.;
    for (const auto& f : region.surface.faces) {
        const Vec3d normal = (region.surface.vertices[f[1]] - region.surface.vertices[f[0]])
                                .cross(region.surface.vertices[f[2]] - region.surface.vertices[f[0]]).normalized();
        steepest = std::max(steepest, std::acos(std::clamp(normal.z(), -1., 1.)) * 180. / std::acos(-1.));
    }
    CHECK(steepest > 75.);
    // Directed analytic-curve -> imported polyline distances. The nearest point
    // is searched on every finite segment, rather than subtracting Z values.
    // Mean/RMS use chord-length quadrature; these samples do not prove a maximum.
    constexpr int subdivisions = 32;
    double maximum = 0., weighted = 0., squared = 0., total_weight = 0., endpoint_displacement = 0.;
    std::size_t sample_count = 0;
    for (std::size_t i = 0; i < discretized.samples.size(); ++i)
        endpoint_displacement = std::max(endpoint_displacement, (region.surface.vertices[i] - discretized.samples[i].point).norm());
    for (std::size_t i = 1; i < discretized.samples.size(); ++i) {
        const double weight = (region.surface.vertices[i] - region.surface.vertices[i - 1]).norm() / subdivisions;
        for (int j = 0; j < subdivisions; ++j) {
            const double t = discretized.samples[i - 1].parameter + (j + .5) / subdivisions *
                (discretized.samples[i].parameter - discretized.samples[i - 1].parameter);
            const Vec3d point = curve.point(t);
            double distance_squared = std::numeric_limits<double>::infinity();
            for (std::size_t k = 1; k < discretized.samples.size(); ++k) {
                const Vec3d a = region.surface.vertices[k - 1], delta = region.surface.vertices[k] - a;
                const double fraction = std::clamp((point - a).dot(delta) / delta.squaredNorm(), 0., 1.);
                distance_squared = std::min(distance_squared, (point - (a + fraction * delta)).squaredNorm());
            }
            const double distance = std::sqrt(distance_squared);
            maximum = std::max(maximum, distance);
            weighted += weight * distance;
            squared += weight * distance_squared;
            total_weight += weight;
            ++sample_count;
        }
    }
    // The independent whole-interval Taylor bound covers positions between
    // samples. Endpoint substitution and double arithmetic are charged separately.
    const double arithmetic_allowance = 128. * std::numeric_limits<double>::epsilon() * 100.;
    const double curve_bound = FH::add_error_bounds(discretized.interpolation_bound,
        FH::add_error_bounds(endpoint_displacement, arithmetic_allowance));
    CHECK(maximum <= curve_bound);
    CHECK(curve_bound < .01);
    std::cout << std::setprecision(17) << "ACCEPTANCE_NACA_CURVE,samples=" << sample_count << ",sampled_maximum_mm=" << maximum
              << ",chord_weighted_mean_mm=" << weighted / total_weight << ",chord_weighted_rms_mm=" << std::sqrt(squared / total_weight)
              << ",whole_curve_bound_mm=" << curve_bound << ",endpoint_displacement_mm=" << endpoint_displacement
              << ",arithmetic_allowance_mm=" << arithmetic_allowance << '\n';
    const auto original = input.solid;
    const auto result = FH::generate(input);
    report("complete_naca0012_negative_real75", result);
    INFO(result.error);
    no_partial_plan(result);
    CHECK(result.error == "The desired nozzle direction exceeds the configured tilt and deviation.");
    CHECK(input.solid.vertices == original.vertices);
    CHECK(input.solid.faces == original.faces);
    std::cout << "ACCEPTANCE_NACA,chord_mm=100,span_mm=2,selected_faces=" << region.surface.faces.size()
              << ",nozzle_mm=" << region.process.nozzle_diameter << ",maximum_facet_tilt_deg=" << steepest << ",curve_chord_bound_mm=" << discretized.interpolation_bound
              << ",cad_signed_distance_available=0\n";
}

TEST_CASE("Integrated curved and inclined previews retain explicit reasons for withholding whole body accuracy", "[FormorixAcceptance]")
{
    const int shape = GENERATE(0, 1, 2, 3);
    constexpr double radius = 20.;
    std::vector<double> coordinates;
    for (int i = 0; i <= 8; ++i) coordinates.push_back(-2. + .5 * i);
    auto input = top_body(coordinates, coordinates, [=](double x, double y) {
        if (shape == 0) return 5. + .25 * x;
        if (shape == 1) return 5. - radius + std::sqrt(radius * radius - x * x);
        if (shape == 2) return 5. + radius - std::sqrt(radius * radius - x * x);
        return 5. - radius + std::sqrt(radius * radius - x * x - y * y);
    });
    auto& region = input.regions.front();
    if (shape == 0) region.cad_reference = FG::plane({0., 0., 5.}, Vec3d(-.25, 0., 1.).normalized());
    else if (shape == 3) region.cad_reference = FG::sphere({0., 0., 5. - radius}, radius);
    else {
        region.cad_reference = FG::cylinder({0., 0., shape == 1 ? 5. - radius : 5. + radius}, Vec3d::UnitY(), radius);
        if (shape == 2) {
            const auto distance = region.cad_reference->signed_distance;
            region.cad_reference->signed_distance = [distance](const Vec3d& p) { return -distance(p); };
        }
    }
    REQUIRE(FG::valid_solid(input.solid));
    const auto import = FG::measure(region.surface, *region.cad_reference);
    REQUIRE(import.bounded);
    CHECK(import.upper_bound < .01);
    const auto original = input.solid;
    const auto result = FH::generate(input);
    const char* names[] = {"inclined_plane", "convex_cylinder", "concave_cylinder", "doubly_curved_sphere"};
    report(names[shape], result);
    INFO(result.error);
    INFO(result.material.error);
    // Pin the observed failure stage, without depending on CGAL work/cell IDs.
    // A future fix must update this negative regression with stronger evidence.
    REQUIRE(result.ok());
    REQUIRE(result.regions.size() == 1);
    CHECK(FG::valid_solid(result.core));
    const auto expected_status = std::array<FM::Status, 4>{FM::Status::BooleanFailure, FM::Status::InvalidSweep,
                                                          FM::Status::BooleanFailure, FM::Status::InvalidSweep}[shape];
    CHECK(result.material.status == expected_status);
    REQUIRE_FALSE(result.material.ok());
    REQUIRE_FALSE(result.material.error.empty());
    const auto& accuracy = result.regions.front().accuracy;
    CHECK_FALSE(accuracy.verified());
    CHECK(std::isfinite(accuracy.bounds[0]));
    CHECK(accuracy.bounds[0] < .01);
    CHECK_FALSE(accuracy.material_partition_verified);
    CHECK_FALSE(accuracy.whole_surface_covered);
    CHECK_FALSE(std::isfinite(accuracy.bounds[4]));
    CHECK_FALSE(std::isfinite(accuracy.total_bound()));
    CHECK(input.solid.vertices == original.vertices);
    CHECK(input.solid.faces == original.faces);
    std::cout << "ACCEPTANCE_IMPORT," << names[shape] << ",maximum=" << import.maximum << ",mean=" << import.mean
              << ",rms=" << import.rms << ",upper=" << import.upper_bound << '\n';
}

TEST_CASE("A complete wrapped cylinder is refused at seventy five degrees without a partial motion track", "[FormorixAcceptance]")
{
    FH::Input input;
    constexpr int count = 32;
    for (int i = 0; i < count; ++i) {
        const double angle = 2. * std::acos(-1.) * i / count;
        for (double y : {0., 3.}) input.solid.vertices.emplace_back(3. * std::cos(angle), y, 3. + 3. * std::sin(angle));
    }
    for (int i = 0; i < count; ++i) {
        const int next = (i + 1) % count;
        input.solid.faces.emplace_back(2 * i, 2 * i + 1, 2 * next);
        input.solid.faces.emplace_back(2 * next, 2 * i + 1, 2 * next + 1);
    }
    FH::RegionInput region;
    region.id = 1;
    region.surface = input.solid;
    region.process.material_id = "acceptance-PETG";
    region.process.nozzle_profile = "acceptance-0.8";
    region.process.nozzle_diameter = .8;
    region.process.line_width = .8;
    region.settings.layers = 1;
    region.settings.layer_height = .2;
    region.settings.allow_vertical = true;
    region.max_tilt = 75.;
    input.regions.push_back(region);
    input.solid.vertices.emplace_back(0., 0., 3.);
    input.solid.vertices.emplace_back(0., 3., 3.);
    for (int i = 0; i < count; ++i) {
        const int next = (i + 1) % count;
        input.solid.faces.emplace_back(2 * count, 2 * i, 2 * next);
        input.solid.faces.emplace_back(2 * count + 1, 2 * next + 1, 2 * i + 1);
    }
    REQUIRE(FG::valid_solid(input.solid));
    const auto result = FH::generate(input);
    report("wrapped_cylinder_real75", result);
    INFO(result.error);
    no_partial_plan(result);
    CHECK(result.error == "Requested surface exceeds nominal nozzle tilt");
}
