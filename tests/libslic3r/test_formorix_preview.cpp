#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "catch2/matchers/catch_matchers.hpp"
#include "libslic3r/FormorixPreview.hpp"
#include "libslic3r/FormorixSurfaces.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/AABBMesh.hpp"
#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

using namespace Slic3r;
namespace FP = Slic3r::FormorixPreview;
namespace FS = Slic3r::FormorixSurfaces;
using Catch::Matchers::WithinAbs;

namespace {
// The selected patch is inset in a closed solid, including for a concave surface.
FP::Input fixture(int shape)
{
    FP::Input input;
    constexpr int n = 24;
    auto z          = [shape](double x, double y) {
        switch (shape) {
        case 0: return 8. + .2 * x + .1 * y;
        case 1: return 9. + 2. * std::cos(x * .09) * std::cos(y * .09);
        case 2: return 9. - 2. * std::cos(x * .09) * std::cos(y * .09);
        case 3: return 8.;
        case 4: {
            const double u = (x + 13.) / 26.;
            return 8. + 26. * .6 * (.2969 * std::sqrt(u) - .126 * u - .3516 * u * u + .2843 * u * u * u - .1015 * u * u * u * u);
        } // NACA 0012 upper foil
        default: return 8. + .25 * std::cos(x * 1.8);
        }
    };
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i)
            input.solid.vertices.emplace_back(float(i - 12), float(j - 12), float(z(i - 12, j - 12)));
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            int a = j * (n + 1) + i, b = a + 1, c = a + n + 1, d = c + 1;
            for (const Vec3i32& t : {Vec3i32(a, b, d), Vec3i32(a, d, c)}) {
                input.solid.indices.push_back(t);
                (i >= 3 && i < 21 && j >= 3 && j < 21 ? input.surface : input.remainder).indices.push_back(t);
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
        input.solid.vertices.emplace_back(input.solid.vertices[i].x(), input.solid.vertices[i].y(), 0.f);
    const int center = int(input.solid.vertices.size());
    input.solid.vertices.emplace_back(0.f, 0.f, 0.f);
    for (int i = 0; i < int(ring.size()); ++i) {
        int j = (i + 1) % int(ring.size());
        for (const Vec3i32& t :
             {Vec3i32(ring[i], start + i, start + j), Vec3i32(ring[i], start + j, ring[j]), Vec3i32(center, start + j, start + i)}) {
            input.solid.indices.push_back(t);
            input.remainder.indices.push_back(t);
        }
    }
    input.surface.vertices   = input.solid.vertices;
    input.remainder.vertices = input.solid.vertices;
    return input;
}
FS::Settings settings()
{
    FS::Settings s;
    s.layers       = 4;
    s.layer_height = .2;
    return s;
}
FP::Options options()
{
    FP::Options o;
    o.line_width = .8;
    o.speed      = 40.;
    return o;
}
} // namespace

TEST_CASE("Smooth patches produce continuous nonplanar layers with a faithful outer contour", "[FormorixPreview]")
{
    const int shape        = GENERATE(0, 1, 2, 4);
    const auto input       = fixture(shape);
    auto s                 = settings();
    s.alternate_directions = false;
    const auto result      = FP::generate(input, s, options());
    INFO("shape=" << shape << " issue=" << (result.errors.empty() ? -1 : int(result.errors.front())));
    REQUIRE(result.ok());
    REQUIRE(result.layers.size() == 4);
    const auto& outer = result.layers.back().path;
    REQUIRE(outer.size() > 100);
    CHECK_THAT(result.layers.back().depth, WithinAbs(0., 1e-9));
    CHECK_THAT(result.shell_depth, WithinAbs(.8, 1e-8));
    AABBMesh source(input.surface);
    for (std::size_t i = 1; i < outer.size(); i += 11) {
        CHECK(source.squared_distance(outer[i].position) < 1e-10);
        CHECK(source.squared_distance((outer[i - 1].position + outer[i].position) * .5) < 1e-10);
    }
    for (std::size_t l = 1; l < result.layers.size(); ++l) {
        REQUIRE(result.layers[l].path.size() == outer.size());
        for (std::size_t i = 0; i < outer.size(); i += 17) {
            const auto &a = result.layers[l - 1].path[i], &b = result.layers[l].path[i];
            CHECK_THAT((b.position - a.position).norm(), WithinAbs(s.layer_height, 1e-6));
            CHECK_THAT((b.position - a.position).normalized().dot(b.axis), WithinAbs(1., 1e-5));
            CHECK_THAT(b.axis.norm(), WithinAbs(1., 1e-8));
        }
    }
    const auto& track = result.timeline.heads[0];
    int travel        = 0;
    REQUIRE(result.timeline.valid());
    for (std::size_t i = 0; i < track.size(); ++i) {
        if (track[i].kind == FP::MotionKind::Travel)
            ++travel;
        if (i)
            CHECK_THAT((track[i - 1].to.position - track[i].from.position).norm(), WithinAbs(0., 1e-9));
        if (track[i].extrusion)
            CHECK_THAT(track[i].end - track[i].start,
                       WithinAbs((track[i].to.position - track[i].from.position).norm() / options().speed, 1e-8));
    }
    CHECK(travel == 3);
    CHECK(result.timeline.heads[1].empty());
    CHECK(result.timeline.heads[2].empty());
    const auto& first = track[track.size() / 2];
    auto sampled      = result.timeline.sample(0, (first.start + first.end) * .5);
    CHECK(sampled.pose_known);
    CHECK_THAT((sampled.pose.position - (first.from.position + first.to.position) * .5).norm(), WithinAbs(0., 1e-6));
    for (int head : {1, 2}) {
        CHECK_FALSE(result.timeline.sample(head, 0.).pose_known);
        CHECK_FALSE(result.timeline.sample(head, result.timeline.duration()).pose_known);
    }
}

TEST_CASE("Thickness and full wall modes control layer depth without expanding the model", "[FormorixPreview]")
{
    auto s            = settings();
    s.shell_mode      = FS::ShellMode::Thickness;
    s.shell_thickness = .65;
    auto result       = FP::generate(fixture(3), s, options());
    REQUIRE(result.ok());
    CHECK(result.layers.size() == 4);
    CHECK_THAT(result.shell_depth, WithinAbs(.65, 1e-8));
    s.shell_mode   = FS::ShellMode::FullWall;
    s.layer_height = 1.;
    result         = FP::generate(fixture(3), s, options());
    INFO("full-wall issue=" << (result.errors.empty() ? -1 : int(result.errors.front())));
    REQUIRE(result.ok());
    CHECK(result.layers.size() == 8);
    CHECK(result.full_wall);
    s.shell_mode      = FS::ShellMode::Thickness;
    s.shell_thickness = 9.;
    result            = FP::generate(fixture(3), s, options());
    REQUIRE_FALSE(result.ok());
    CHECK(result.errors.front() == FP::Issue::ThinWall);
}

TEST_CASE("Vertical walls require explicit permission and bounded nozzle deviation", "[FormorixPreview]")
{
    auto input = fixture(3);
    const Eigen::AngleAxisd rotation(3.14159265358979323846 * .5, Vec3d::UnitY());
    for (auto* mesh : {&input.surface, &input.solid, &input.remainder})
        for (auto& v : mesh->vertices)
            v = (rotation * v.cast<double>()).cast<float>();
    auto s      = settings();
    auto result = FP::generate(input, s, options());
    REQUIRE_FALSE(result.ok());
    CHECK(result.errors.front() == FP::Issue::VerticalDisabled);
    s.allow_vertical = true;
    result           = FP::generate(input, s, options());
    REQUIRE_FALSE(result.ok());
    CHECK(result.errors.front() == FP::Issue::TiltExceeded);
    s.allow_deviation = true;
    s.max_deviation   = 15.;
    result            = FP::generate(input, s, options());
    REQUIRE(result.ok());
    for (const auto& pose : result.layers.back().path) {
        CHECK_THAT(std::acos(pose.axis.z()) * 180. / 3.14159265358979323846, WithinAbs(75., 1e-5));
        CHECK_THAT(std::acos(pose.axis.x()) * 180. / 3.14159265358979323846, WithinAbs(15., 1e-5));
    }
}

TEST_CASE("Narrow concave offsets and disconnected selections fail without partial paths", "[FormorixPreview]")
{
    auto input  = fixture(5);
    auto s      = settings();
    s.layers    = 15;
    auto result = FP::generate(input, s, options());
    REQUIRE_FALSE(result.ok());
    CHECK(result.errors.front() == FP::Issue::InvalidOffset);
    CHECK(result.timeline.heads[0].empty());
    input     = fixture(3);
    auto& tri = input.surface.indices;
    tri.erase(std::remove_if(tri.begin(), tri.end(),
                             [&](const Vec3i32& t) {
                                 double x = 0.;
                                 for (int i : t)
                                     x += input.surface.vertices[i].x();
                                 return std::abs(x / 3.) < 1.1;
                             }),
              tri.end());
    result = FP::generate(input, settings(), options());
    REQUIRE_FALSE(result.ok());
    CHECK(result.errors.front() == FP::Issue::Disconnected);
    result = FP::generate(fixture(1), settings(), options(), [] { return true; });
    CHECK(result.errors.front() == FP::Issue::Canceled);
}

TEST_CASE("Merged region masks create one path across their former boundary", "[FormorixPreview]")
{
    const auto input = fixture(1);
    Model model;
    auto* object = model.add_object();
    auto* volume = object->add_volume(TriangleMesh(input.solid), ModelVolumeType::MODEL_PART, false);
    object->add_instance();
    TriangleSelector paint(volume->mesh());
    for (std::size_t i = 0; i < input.surface.indices.size(); ++i) {
        const auto& t = input.surface.indices[i];
        auto found    = std::find(input.solid.indices.begin(), input.solid.indices.end(), t);
        REQUIRE(found != input.solid.indices.end());
        paint.set_facet(int(found - input.solid.indices.begin()), EnforcerBlockerType(input.solid.vertices[t[0]].x() < 0 ? 1 : 2));
    }
    volume->formorix_facets.set(paint);
    volume->formorix_topology = FS::topology_id(volume->mesh().its);
    FS::Project p;
    p.regions             = {{1, "left"}, {2, "right"}};
    p.regions[0].settings = settings();
    FS::merge_regions(p, model, {1, 2}, 1);
    const auto result = FP::generate(FP::extract(*object, 0, 1), p.regions[0].settings, options());
    INFO("merged issue=" << (result.errors.empty() ? -1 : int(result.errors.front())));
    REQUIRE(result.ok());
    int crossings = 0;
    for (const auto& m : result.timeline.heads[0])
        if (m.from.position.x() * m.to.position.x() < 0) {
            ++crossings;
            CHECK(m.extrusion);
        }
    CHECK(crossings > 5);
    auto edge          = settings();
    edge.edge_selected = true;
    edge.edge          = {-9., -9., double(input.solid.vertices[3 * 25 + 3].z()), -8., -9., double(input.solid.vertices[3 * 25 + 4].z())};
    const auto local   = FP::reference_direction(*object, 0, edge);
    object->instances[0]->set_rotation(Vec3d(0., 0., .5));
    CHECK_THAT((FP::reference_direction(*object, 0, edge) - object->instances[0]->get_matrix().linear() * local).norm(),
               WithinAbs(0., 1e-8));
    edge.edge[0] = 200.;
    CHECK_THROWS(FP::reference_direction(*object, 0, edge));
}

TEST_CASE("Preview rejects holes, open solids, invalid limits and unsupported full-wall variation", "[FormorixPreview]")
{
    auto input = fixture(3);
    input.solid.indices.pop_back();
    auto result = FP::generate(input, settings(), options());
    REQUIRE_FALSE(result.ok());
    CHECK(result.errors.front() == FP::Issue::InvalidMesh);
    input = fixture(3);
    input.surface.indices.erase(input.surface.indices.begin() + 300);
    result = FP::generate(input, settings(), options());
    REQUIRE_FALSE(result.ok());
    CHECK(result.errors.front() == FP::Issue::NonDisk);
    auto o         = options();
    o.max_segments = 10;
    result         = FP::generate(fixture(1), settings(), o);
    REQUIRE_FALSE(result.ok());
    CHECK(result.errors.front() == FP::Issue::TooLarge);
    auto s       = settings();
    s.shell_mode = FS::ShellMode::FullWall;
    result       = FP::generate(fixture(1), s, options());
    REQUIRE_FALSE(result.ok());
    CHECK(result.errors.front() == FP::Issue::FullWallVariable);
    s                  = settings();
    s.direction        = FS::Direction::Custom;
    s.custom_direction = {0., 0., 1.};
    result             = FP::generate(fixture(3), s, options());
    REQUIRE_FALSE(result.ok());
    CHECK(result.errors.front() == FP::Issue::NotHeightField);
    s                   = settings();
    s.boundary_margin   = 1.;
    s.fine_final_layers = 2;
    s.fine_layer_height = .1;
    o                   = options();
    o.head              = 2;
    result              = FP::generate(fixture(3), s, o);
    REQUIRE(result.ok());
    CHECK(result.timeline.heads[0].empty());
    CHECK_FALSE(result.timeline.heads[2].empty());
    CHECK_FALSE(result.timeline.sample(0, 0.).pose_known);
    CHECK_FALSE(result.timeline.sample(1, 0.).pose_known);
    CHECK(result.timeline.sample(2, 0.).pose_known);
    CHECK_THAT(result.shell_depth, WithinAbs(.6, 1e-8));
    for (const auto& pose : result.layers.back().path) {
        CHECK(std::abs(pose.position.x()) <= 8. + 1e-5);
        CHECK(std::abs(pose.position.y()) <= 8. + 1e-5);
    }
}

TEST_CASE("Empty preview tracks never provide nozzle positions at any seek time", "[FormorixPreview]")
{
    const FP::Timeline timeline;
    REQUIRE(timeline.valid());
    CHECK_THAT(timeline.duration(), WithinAbs(0., 0.));
    for (int head = 0; head < 3; ++head)
        for (double time : {-10., 0., 1., 100.}) {
            const auto state = timeline.sample(head, time);
            CHECK_FALSE(state.pose_known);
            CHECK(state.kind == FP::MotionKind::Unassigned);
            CHECK_FALSE(state.extrusion);
            CHECK_FALSE(state.process.has_value());
        }
}

TEST_CASE("Only the head with an actual motion has a known preview pose", "[FormorixPreview]")
{
    const int active = GENERATE(0, 1, 2);
    FP::Timeline timeline;
    const FP::Pose from{{21., 7., 3.}, Vec3d::UnitZ()}, to{{25., 7., 3.}, Vec3d::UnitZ()};
    timeline.heads[active] = {{active, 2., 4., from, to, FP::MotionKind::Extrude, true, 0, 1}};
    REQUIRE(timeline.valid());
    for (int head = 0; head < 3; ++head)
        for (double time : {0., 3., 5.}) {
            const auto state = timeline.sample(head, time);
            if (head != active) {
                CHECK_FALSE(state.pose_known);
                CHECK(state.kind == FP::MotionKind::Unassigned);
                CHECK_FALSE(state.extrusion);
                continue;
            }
            REQUIRE(state.pose_known);
            CHECK_THAT(state.pose.position.x(), WithinAbs(time < 2. ? 21. : time > 4. ? 25. : 23., 1e-12));
            CHECK_THAT(state.pose.position.y(), WithinAbs(7., 0.));
            CHECK_THAT(state.pose.position.z(), WithinAbs(3., 0.));
            CHECK(state.kind == (time == 3. ? FP::MotionKind::Extrude : FP::MotionKind::Wait));
            CHECK(state.extrusion == (time == 3.));
        }
}

TEST_CASE("Independent head tracks interpolate on a common clock including gaps and unassigned heads", "[FormorixPreview]")
{
    FP::Timeline timeline;
    timeline.heads[0] = {{0, 0., 2., {{0, 0, 0}, {0, 0, 1}}, {{10, 0, 0}, {1, 0, 0}}, FP::MotionKind::Extrude, true, 0, 1},
                         {0, 4., 5., {{10, 0, 0}, {1, 0, 0}}, {{10, 5, 0}, {1, 0, 0}}, FP::MotionKind::Travel, false, 1, 1}};
    timeline.heads[1] = {{1, 1., 3., {{0, 0, 0}, {0, 0, 1}}, {{0, 8, 0}, {0, 0, 1}}, FP::MotionKind::Extrude, true, 2, 2}};
    REQUIRE(timeline.valid());
    CHECK_THAT(timeline.duration(), WithinAbs(5., 1e-12));
    auto a = timeline.sample(0, 1.);
    REQUIRE(a.pose_known);
    CHECK_THAT(a.pose.position.x(), WithinAbs(5., 1e-12));
    CHECK_THAT(a.pose.axis.x(), WithinAbs(std::sqrt(.5), 1e-9));
    auto b = timeline.sample(1, 2.);
    REQUIRE(b.pose_known);
    CHECK_THAT(b.pose.position.y(), WithinAbs(4., 1e-12));
    CHECK(b.extrusion);
    const auto before_first = timeline.sample(1, 0.);
    CHECK(before_first.pose_known);
    CHECK(before_first.kind == FP::MotionKind::Wait);
    CHECK_THAT((before_first.pose.position - timeline.heads[1].front().from.position).norm(), WithinAbs(0., 0.));
    const auto gap = timeline.sample(0, 3.);
    CHECK(gap.pose_known);
    CHECK(gap.kind == FP::MotionKind::Wait);
    CHECK_FALSE(gap.extrusion);
    CHECK_THAT((gap.pose.position - timeline.heads[0].front().to.position).norm(), WithinAbs(0., 0.));
    CHECK_THAT((gap.pose.axis - timeline.heads[0].front().to.axis).norm(), WithinAbs(0., 0.));
    const auto unassigned = timeline.sample(2, 2.);
    CHECK_FALSE(unassigned.pose_known);
    CHECK(unassigned.kind == FP::MotionKind::Unassigned);
    const auto after_end = timeline.sample(0, 6.);
    CHECK(after_end.pose_known);
    CHECK(after_end.kind == FP::MotionKind::Wait);
    CHECK_FALSE(after_end.extrusion);
    CHECK_THAT((after_end.pose.position - timeline.heads[0].back().to.position).norm(), WithinAbs(0., 0.));
    CHECK_THAT((after_end.pose.axis - timeline.heads[0].back().to.axis).norm(), WithinAbs(0., 0.));
    CHECK(timeline.sample(0, 4.5).pose_known);
    CHECK(timeline.sample(0, 4.5).kind == FP::MotionKind::Travel);
    CHECK_THAT(timeline.sample(0, 4.5).pose.position.y(), WithinAbs(2.5, 1e-12));
    CHECK_THROWS(timeline.sample(3, 0.));
    CHECK_THROWS(timeline.sample(0, std::numeric_limits<double>::quiet_NaN()));
    timeline.heads[1][0].head = 0;
    CHECK_FALSE(timeline.valid());
}

TEST_CASE("Three independently supplied tracks retain their own known poses and activity", "[FormorixPreview]")
{
    FP::Timeline timeline;
    for (int head = 0; head < 3; ++head) {
        const FP::Pose from{{10. * head, 5. * head, 2. + head}, Vec3d::UnitZ()};
        const FP::Pose to{from.position + Vec3d(2. * (head + 1), 0., 0.), Vec3d::UnitZ()};
        timeline.heads[head] = {{head, double(head), double(head + 2), from, to,
                                 head == 1 ? FP::MotionKind::Travel : FP::MotionKind::Extrude, head != 1, 0, head + 1}};
    }
    REQUIRE(timeline.valid());
    CHECK_THAT(timeline.duration(), WithinAbs(4., 0.));
    const auto first = timeline.sample(0, 2.5), second = timeline.sample(1, 2.5), third = timeline.sample(2, 2.5);
    CHECK(first.pose_known);
    CHECK(second.pose_known);
    CHECK(third.pose_known);
    CHECK(first.kind == FP::MotionKind::Wait);
    CHECK(second.kind == FP::MotionKind::Travel);
    CHECK(third.kind == FP::MotionKind::Extrude);
    CHECK_THAT(first.pose.position.x(), WithinAbs(2., 1e-12));
    CHECK_THAT(second.pose.position.x(), WithinAbs(13., 1e-12));
    CHECK_THAT(third.pose.position.x(), WithinAbs(21.5, 1e-12));
    for (int head = 0; head < 3; ++head) {
        const auto state = timeline.sample(head, 10.);
        CHECK(state.pose_known);
        CHECK(state.kind == FP::MotionKind::Wait);
        CHECK_FALSE(state.extrusion);
        CHECK_THAT((state.pose.position - timeline.heads[head].back().to.position).norm(), WithinAbs(0., 0.));
    }
}
