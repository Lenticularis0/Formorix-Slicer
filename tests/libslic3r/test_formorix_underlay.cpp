#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/FormorixUnderlay.hpp"
#include "libslic3r/FormorixHybrid.hpp"
#include "libslic3r/FormorixCorePreview.hpp"
#include "libslic3r/FormorixGeometry.hpp"
#include "libslic3r/FormorixProcess.hpp"
#include "libslic3r/FormorixSurfaces.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <set>
#include <utility>

using namespace Slic3r;
namespace FU = FormorixUnderlay;
namespace FH = FormorixHybrid;
namespace CP = FormorixCorePreview;
namespace FG = FormorixGeometry;
namespace PS = FormorixProcess;
namespace FS = FormorixSurfaces;
using Catch::Matchers::WithinAbs;
namespace {
Polygon rectangle(double x0, double y0, double x1, double y1)
{ return Polygon(Points{Point::new_scale(x0, y0), Point::new_scale(x1, y0), Point::new_scale(x1, y1), Point::new_scale(x0, y1)}); }
FH::Result interface(double z = 1., double x = 2., double y = 2.)
{
    FH::Result result;
    // Only the nonempty-core identity is used by prepare; native integration
    // tests elsewhere supply a real partition and actual Orca extrusion paths.
    result.core.vertices = {{x, y, 0.}, {x + 1., y, 0.}, {x, y + 1., 0.}};
    result.core.faces    = {{0, 1, 2}};
    FG::Interface shared;
    shared.first         = 0;
    shared.second        = 1;
    shared.mesh.vertices = {{x, y, z}, {x + 1., y, z}, {x + 1., y + 1., z}, {x, y + 1., z}};
    shared.mesh.faces    = {{0, 1, 2}, {0, 2, 3}};
    result.partition.interfaces.push_back(std::move(shared));
    FH::RegionResult region;
    region.id                        = 1;
    region.head                      = 2;
    region.process.material_id       = "PETG-CF";
    region.process.layer_height      = .2;
    region.process.line_width        = .4;
    region.settings.dense_underlay   = true;
    region.settings.underlay_density = 60.;
    result.regions.push_back(std::move(region));
    return result;
}
CP::Result native(double x = 0., double y = 0.)
{
    CP::Result out;
    CP::LayerSlice layer;
    layer.layer_id = 4;
    layer.head     = 0;
    layer.area.emplace_back(rectangle(x, y, x + 10., y + 10.));
    layer.bottom_z = layer.print_bottom_z = .8;
    layer.top_z = layer.print_top_z = 1.;
    layer.slice_z                   = .9;
    out.layers.push_back(std::move(layer));
    CP::Path path;
    path.head                = 0;
    path.process.material_id = "PETG";
    out.paths.push_back(std::move(path));
    return out;
}
void contacts(FH::Result& result, double gap = std::numeric_limits<double>::infinity(), FH::UnderlayKind kind = FH::UnderlayKind::PathGap)
{
    for (auto& region : result.regions) {
        region.underlay_contacts.clear();
        region.underlay = {};
        for (std::size_t i = 0; i < region.interface_samples.size(); ++i) {
            FH::UnderlayContact contact;
            contact.position = region.interface_samples[i];
            contact.inward   = -region.interface_normals[i];
            contact.raw_gap  = gap;
            contact.gap      = kind == FH::UnderlayKind::UnknownGeometry ? std::numeric_limits<double>::infinity() : gap;
            contact.kind     = kind;
            region.underlay_contacts.push_back(contact);
            ++region.underlay.samples;
            if (gap > .05)
                ++region.underlay.raw_missing;
            if (kind != FH::UnderlayKind::Contact)
                ++region.underlay.missing;
            if (kind == FH::UnderlayKind::PathGap)
                ++region.underlay.path_gaps;
        }
    }
}
double area_mm2(const ExPolygons& polygons)
{
    double sum = 0.;
    for (const auto& polygon : polygons)
        sum += polygon.area() * SCALING_FACTOR * SCALING_FACTOR;
    return sum;
}
int surface_operation(const PS::Dependencies& dependencies)
{
    const auto found = std::find_if(dependencies.operations.begin(), dependencies.operations.end(),
                                    [](const auto& operation) { return operation.kind == PS::OperationKind::Surface; });
    REQUIRE(found != dependencies.operations.end());
    return found->id;
}
} // namespace

TEST_CASE("Underlay interface probes preserve source triangles and finite area weights", "[FormorixUnderlay]")
{
    for (bool reverse : {false, true}) {
        auto result  = interface();
        auto& source = result.partition.interfaces.front();
        if (reverse) {
            std::swap(source.first, source.second);
            for (auto& face : source.mesh.faces)
                std::swap(face[1], face[2]);
        }
        const auto prepared = FU::prepare(result);
        INFO(prepared.error);
        REQUIRE(prepared.ok());
        REQUIRE(prepared.regions.size() == 1);
        REQUIRE(prepared.regions.front().probes.size() > 2);
        double weight = 0.;
        for (const auto& probe : prepared.regions.front().probes) {
            weight += probe.area;
            const auto& face  = source.mesh.faces[probe.source_face];
            const Vec3d point = probe.barycentric[0] * source.mesh.vertices[face[0]] +
                                probe.barycentric[1] * source.mesh.vertices[face[1]] + probe.barycentric[2] * source.mesh.vertices[face[2]];
            CHECK_THAT((point - probe.position).norm(), WithinAbs(0., 1e-12));
            CHECK_THAT((probe.outward - Vec3d::UnitZ()).norm(), WithinAbs(0., 1e-12));
            CHECK(probe.area > 0.);
            for (int i = 0; i < 3; ++i)
                CHECK((probe.footprint[i] - probe.footprint[(i + 1) % 3]).norm() <= .5);
        }
        CHECK_THAT(weight, WithinAbs(1., 1e-12));
        CHECK_THAT(prepared.regions.front().interface_area, WithinAbs(1., 1e-12));
        CHECK_FALSE(prepared.regions.front().external_support_required);
        CHECK_FALSE(result.regions.front().underlay.coverage_bounded);
    }
}

TEST_CASE("Dense underlay requests stay local and preserve the explicitly selected density", "[FormorixUnderlay]")
{
    for (bool enabled : {false, true}) {
        CAPTURE(enabled);
        auto result                                    = interface();
        result.regions.front().settings.dense_underlay = enabled;
        const auto prepared                            = FU::prepare(result);
        REQUIRE(prepared.ok());
        contacts(result);
        const auto core     = native();
        const auto analysis = FU::analyze(result, prepared, core);
        INFO(analysis.error);
        REQUIRE(analysis.ok());
        REQUIRE(analysis.regions.size() == 1);
        CHECK_THAT(analysis.regions.front().path_gap_area, WithinAbs(1., 1e-12));
        CHECK_THAT(analysis.regions.front().native_layer_gap_area, WithinAbs(0., 0.));
        CHECK(result.manufacturing_dependencies.valid());
        CHECK_FALSE(result.manufacturing_dependencies.prerequisites_complete(surface_operation(result.manufacturing_dependencies), {}));
        if (enabled) {
            REQUIRE(analysis.dense_requests.size() == 1);
            const auto& request = analysis.dense_requests.front();
            CHECK_THAT(request.density, WithinAbs(60., 0.));
            CHECK_THAT(request.bottom_z, WithinAbs(.8, 0.));
            CHECK_THAT(request.top_z, WithinAbs(1., 0.));
            CHECK_THAT(request.slice_z, WithinAbs(.9, 0.));
            CHECK(diff_ex(request.area, core.layers.front().area).empty());
            CHECK(area_mm2(request.area) > 1.);
            CHECK(area_mm2(request.area) < 2.);
            CHECK(std::any_of(result.manufacturing_dependencies.operations.begin(), result.manufacturing_dependencies.operations.end(),
                              [](const auto& operation) { return operation.kind == PS::OperationKind::DenseTransition; }));
        } else {
            CHECK(analysis.dense_requests.empty());
        }
        CHECK_FALSE(analysis.regions.front().coverage_bounded);
    }
}

TEST_CASE("A native layer staircase gap cannot be repaired by densifying unreachable infill", "[FormorixUnderlay]")
{
    auto result         = interface(1.15);
    const auto prepared = FU::prepare(result);
    REQUIRE(prepared.ok());
    contacts(result, .15);
    const auto analysis = FU::analyze(result, prepared, native());
    INFO(analysis.error);
    REQUIRE(analysis.ok());
    CHECK(analysis.dense_requests.empty());
    CHECK_THAT(analysis.regions.front().native_layer_gap_area, WithinAbs(1., 1e-12));
    CHECK_THAT(analysis.regions.front().path_gap_area, WithinAbs(0., 0.));
    CHECK(result.regions.front().underlay.path_gaps == 0);
    CHECK(result.regions.front().underlay.native_layer_gaps == prepared.regions.front().probes.size());
    for (const auto& sample : analysis.regions.front().samples) {
        CHECK(sample.kind == FU::RawKind::NativeLayerGap);
        CHECK_THAT(sample.native_prism_gap, WithinAbs(.15, 1e-12));
    }
    const auto failed = FU::analyze(result, prepared, native(), {}, [] { return true; });
    CHECK(failed.status == FU::Status::Canceled);
    CHECK(result.native_layer_gaps.empty());
    CHECK(result.regions.front().underlay.native_layer_gaps == 0);
    CHECK(result.manufacturing_dependencies.operations.empty());
}

TEST_CASE("An applied native dense transition remains an uncompleted prerequisite after its gaps close", "[FormorixUnderlay]")
{
    auto result         = interface();
    const auto prepared = FU::prepare(result);
    REQUIRE(prepared.ok());
    contacts(result, 0., FH::UnderlayKind::Contact);
    auto core = native();
    core.applied_dense_regions.push_back(1);
    const auto analysis = FU::analyze(result, prepared, core);
    REQUIRE(analysis.ok());
    CHECK(analysis.dense_requests.empty());
    const auto& dependencies = result.manufacturing_dependencies;
    REQUIRE(dependencies.valid());
    const auto dense = std::find_if(dependencies.operations.begin(), dependencies.operations.end(),
                                    [](const auto& op) { return op.kind == PS::OperationKind::DenseTransition; });
    REQUIRE(dense != dependencies.operations.end());
    CHECK_FALSE(dependencies.prerequisites_complete(surface_operation(dependencies), {}));
}

TEST_CASE("Native underlay contact uses print heights while modifier requests retain geometric source heights", "[FormorixUnderlay]")
{
    for (bool reachable : {false, true}) {
        auto result         = interface(reachable ? 1.6 : 1.);
        const auto prepared = FU::prepare(result);
        REQUIRE(prepared.ok());
        contacts(result);
        auto core                          = native();
        core.layers.front().print_bottom_z = 1.4;
        core.layers.front().print_top_z    = 1.6;
        const auto analysis                = FU::analyze(result, prepared, core);
        INFO(analysis.error);
        REQUIRE(analysis.ok());
        if (reachable) {
            REQUIRE(analysis.dense_requests.size() == 1);
            CHECK_THAT(analysis.dense_requests.front().bottom_z, WithinAbs(.8, 0.));
            CHECK_THAT(analysis.dense_requests.front().top_z, WithinAbs(1., 0.));
        } else {
            CHECK(analysis.dense_requests.empty());
            CHECK_THAT(analysis.regions.front().native_layer_gap_area, WithinAbs(1., 1e-12));
        }
    }
}

TEST_CASE("Native layer holes remain holes in underlay reachability and local modifier footprints", "[FormorixUnderlay]")
{
    auto core = native();
    auto hole = rectangle(2., 2., 3., 3.);
    hole.make_clockwise();
    core.layers.front().area.front().holes.push_back(hole);
    auto result         = interface();
    const auto prepared = FU::prepare(result);
    REQUIRE(prepared.ok());
    contacts(result);
    const auto analysis = FU::analyze(result, prepared, core);
    REQUIRE(analysis.ok());
    CHECK(analysis.dense_requests.empty());
    CHECK_THAT(analysis.regions.front().native_layer_gap_area, WithinAbs(1., 1e-12));

    result                = interface(1., 1.5, 2.);
    const auto straddling = FU::prepare(result);
    REQUIRE(straddling.ok());
    contacts(result);
    const auto clipped = FU::analyze(result, straddling, core);
    REQUIRE(clipped.ok());
    REQUIRE(clipped.dense_requests.size() == 1);
    CHECK(diff_ex(clipped.dense_requests.front().area, core.layers.front().area).empty());
    CHECK(clipped.regions.front().native_layer_gap_area > 0.);
    CHECK(clipped.regions.front().path_gap_area > 0.);
}

TEST_CASE("Raw contact remains separate from uncalibrated Flow and unsupported material geometry", "[FormorixUnderlay]")
{
    for (bool unsupported : {false, true}) {
        auto result                               = interface();
        result.regions.front().process.flow_ratio = .98;
        const auto prepared                       = FU::prepare(result);
        REQUIRE(prepared.ok());
        contacts(result, 0., FH::UnderlayKind::UnknownGeometry);
        for (auto& contact : result.regions.front().underlay_contacts)
            contact.raw_geometry_unknown = unsupported;
        const auto analysis = FU::analyze(result, prepared, native());
        REQUIRE(analysis.ok());
        CHECK(analysis.dense_requests.empty());
        for (const auto& contact : result.regions.front().underlay_contacts) {
            CHECK(contact.kind == FH::UnderlayKind::UnknownGeometry);
            CHECK(std::isinf(contact.gap));
        }
        for (const auto& sample : analysis.regions.front().samples) {
            const auto expected = unsupported ? FU::RawKind::Unknown : FU::RawKind::Contact;
            CHECK(sample.kind == expected);
        }
        CHECK_FALSE(analysis.regions.front().coverage_bounded);
    }
}

TEST_CASE("A raw infill gap can request geometry without certifying nonunit Flow or excluded infill", "[FormorixUnderlay]")
{
    for (bool excluded : {false, true})
        for (bool enabled : {false, true}) {
            auto result                                    = interface();
            result.regions.front().process.flow_ratio      = .98;
            result.regions.front().settings.use_infill     = false;
            result.regions.front().settings.dense_underlay = enabled;
            const auto prepared                            = FU::prepare(result);
            REQUIRE(prepared.ok());
            const auto kind = excluded ? FH::UnderlayKind::ExcludedInfill : FH::UnderlayKind::UnknownGeometry;
            contacts(result, std::numeric_limits<double>::infinity(), kind);
            const auto analysis = FU::analyze(result, prepared, native());
            INFO(analysis.error);
            REQUIRE(analysis.ok());
            CHECK_THAT(analysis.regions.front().path_gap_area, WithinAbs(1., 1e-12));
            if (enabled)
                REQUIRE(analysis.dense_requests.size() == 1);
            else
                CHECK(analysis.dense_requests.empty());
            for (const auto& contact : result.regions.front().underlay_contacts)
                CHECK(contact.kind == kind);
            CHECK_FALSE(analysis.regions.front().coverage_bounded);
        }
}

TEST_CASE("Excluded ordinary infill requests explicit density before only regenerated dense paths become eligible", "[FormorixUnderlay]")
{
    auto result          = interface();
    result.core.vertices = {{0., 0., 0.}, {5., 0., 0.}, {5., 5., 0.}, {0., 5., 0.}, {0., 0., 1.}, {5., 0., 1.}, {5., 5., 1.}, {0., 5., 1.}};
    result.core.faces    = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                            {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
    REQUIRE(FG::valid_solid(result.core));
    auto& region               = result.regions.front();
    region.settings.use_infill = false;
    const auto prepared        = FU::prepare(result);
    REQUIRE(prepared.ok());
    FH::UnderlaySegment path;
    path.from     = Vec3d(1., 2.5, 1.);
    path.to       = Vec3d(4., 2.5, 1.);
    path.width    = 2.;
    path.height   = .2;
    path.head     = 0;
    path.material = "PETG";
    path.infill   = true;
    for (bool enabled : {false, true}) {
        region.settings.dense_underlay = enabled;
        FH::audit_underlay(result, {path});
        for (const auto& contact : region.underlay_contacts)
            CHECK(contact.kind == FH::UnderlayKind::ExcludedInfill);
        const auto analysis = FU::analyze(result, prepared, native());
        INFO(analysis.error);
        REQUIRE(analysis.ok());
        CHECK_THAT(analysis.regions.front().path_gap_area, WithinAbs(1., 1e-12));
        if (enabled) {
            REQUIRE(analysis.dense_requests.size() == 1);
            CHECK_THAT(analysis.dense_requests.front().density, WithinAbs(60., 0.));
        } else
            CHECK(analysis.dense_requests.empty());
        // An analysis request does not retrospectively authorize ordinary infill.
        for (const auto& contact : region.underlay_contacts)
            CHECK(contact.kind == FH::UnderlayKind::ExcludedInfill);
    }
    path.dense_transition          = true;
    region.settings.dense_underlay = false;
    FH::audit_underlay(result, {path});
    for (const auto& contact : region.underlay_contacts)
        CHECK(contact.kind == FH::UnderlayKind::ExcludedInfill);
    region.settings.dense_underlay = true;
    FH::audit_underlay(result, {path});
    for (const auto& contact : region.underlay_contacts) {
        CHECK(contact.kind == FH::UnderlayKind::Contact);
        CHECK_THAT(contact.gap, WithinAbs(0., 1e-12));
    }
    auto regenerated = native();
    regenerated.applied_dense_regions.push_back(region.id);
    const auto actual = FU::analyze(result, prepared, regenerated);
    REQUIRE(actual.ok());
    CHECK(actual.dense_requests.empty());
    CHECK_FALSE(actual.regions.front().coverage_bounded);
    CHECK_FALSE(result.manufacturing_dependencies.prerequisites_complete(surface_operation(result.manufacturing_dependencies), {}));
}

TEST_CASE("A vertical core interface retains finite local request footprints", "[FormorixUnderlay]")
{
    auto result         = interface();
    auto& shared        = result.partition.interfaces.front().mesh;
    shared.vertices     = {{5., 2., .825}, {5., 3., .825}, {5., 3., .975}, {5., 2., .975}};
    const auto prepared = FU::prepare(result);
    REQUIRE(prepared.ok());
    contacts(result);
    auto core                = native();
    core.layers.front().area = ExPolygons{ExPolygon(rectangle(0., 0., 5., 10.))};
    const auto analysis      = FU::analyze(result, prepared, core);
    INFO(analysis.error);
    REQUIRE(analysis.ok());
    REQUIRE(analysis.dense_requests.size() == 1);
    CHECK(area_mm2(analysis.dense_requests.front().area) > 0.);
    CHECK(area_mm2(analysis.dense_requests.front().area) < 1.);
    CHECK(diff_ex(analysis.dense_requests.front().area, core.layers.front().area).empty());
    CHECK_THAT(analysis.regions.front().path_gap_area, WithinAbs(.15, 1e-12));
}

TEST_CASE("A full 5D wall without a core keeps an unassigned and uncompleted external support dependency", "[FormorixUnderlay]")
{
    auto result = interface();
    result.core = {};
    result.partition.interfaces.clear();
    result.regions.front().settings.shell_mode = FS::ShellMode::FullWall;
    const auto prepared                        = FU::prepare(result);
    REQUIRE(prepared.ok());
    REQUIRE(prepared.regions.size() == 1);
    CHECK(prepared.regions.front().probes.empty());
    CHECK(prepared.regions.front().external_support_required);
    const auto analysis = FU::analyze(result, prepared, CP::Result{});
    INFO(analysis.error);
    REQUIRE(analysis.ok());
    CHECK(analysis.dense_requests.empty());
    CHECK_FALSE(analysis.regions.front().coverage_bounded);
    const auto& dependencies = result.manufacturing_dependencies;
    REQUIRE(dependencies.valid());
    REQUIRE(dependencies.operations.size() == 2);
    CHECK_FALSE(dependencies.operations.front().assigned);
    CHECK(dependencies.operations.front().head == -1);
    CHECK(dependencies.operations.front().material.empty());
    std::set<int> falsely_completed;
    for (const auto& op : dependencies.operations)
        falsely_completed.insert(op.id);
    CHECK_FALSE(dependencies.prerequisites_complete(surface_operation(dependencies), falsely_completed));
}

TEST_CASE("Underlay limits cancellation and stale contacts never leave actionable modifier requests", "[FormorixUnderlay]")
{
    auto result = interface();
    FU::Options tiny;
    tiny.max_samples   = 1;
    const auto limited = FU::prepare(result, tiny);
    CHECK(limited.status == FU::Status::ResourceLimit);
    CHECK(result.regions.front().interface_samples.empty());
    CHECK_FALSE(result.regions.front().underlay.coverage_bounded);
    const auto canceled = FU::prepare(result, {}, [] { return true; });
    CHECK(canceled.status == FU::Status::Canceled);
    const auto prepared = FU::prepare(result);
    REQUIRE(prepared.ok());
    contacts(result);
    tiny                = {};
    tiny.max_edge_tests = 1;
    const auto queries  = FU::analyze(result, prepared, native(), tiny);
    CHECK(queries.status == FU::Status::ResourceLimit);
    CHECK(queries.dense_requests.empty());
    CHECK(result.manufacturing_dependencies.operations.empty());
    result.regions.front().underlay_contacts.front().position.x() += 1.;
    const auto stale = FU::analyze(result, prepared, native());
    CHECK(stale.status == FU::Status::InvalidInput);
    CHECK(stale.dense_requests.empty());
}

TEST_CASE("Underlay layer coordinates remain in source space after native slicing rebases the model", "[FormorixUnderlay]")
{
    auto result         = interface(1., 1588., -300.);
    const auto prepared = FU::prepare(result);
    REQUIRE(prepared.ok());
    contacts(result);
    auto core           = native(1587., -301.);
    core.local_origin   = Vec3d(1590., -295., -.5);
    const auto analysis = FU::analyze(result, prepared, core);
    INFO(analysis.error);
    REQUIRE(analysis.ok());
    REQUIRE(analysis.dense_requests.size() == 1);
    CHECK(diff_ex(analysis.dense_requests.front().area, core.layers.front().area).empty());
    CHECK_THAT(analysis.regions.front().path_gap_area, WithinAbs(1., 1e-12));
}

TEST_CASE("Native modifier polygon limits discard all underlay requests without reducing input layer budgets", "[FormorixUnderlay]")
{
    auto result                                = interface();
    auto second                                = interface(1.2);
    second.regions.front().id                  = 2;
    second.partition.interfaces.front().second = 2;
    result.regions.push_back(second.regions.front());
    result.partition.interfaces.push_back(second.partition.interfaces.front());
    const auto prepared = FU::prepare(result);
    REQUIRE(prepared.ok());
    contacts(result);
    auto core      = native();
    auto layer     = core.layers.front();
    layer.layer_id = 5;
    layer.bottom_z = layer.print_bottom_z = 1.;
    layer.top_z = layer.print_top_z = 1.2;
    layer.slice_z                   = 1.1;
    core.layers.push_back(std::move(layer));
    const auto baseline = FU::analyze(result, prepared, core);
    INFO(baseline.error);
    REQUIRE(baseline.ok());
    REQUIRE(baseline.dense_requests.size() == 2);
    for (bool per_request : {false, true}) {
        FU::Options limited;
        if (per_request)
            limited.max_request_vertices = 3;
        else
            limited.max_total_request_vertices = 7;
        CHECK(limited.max_polygon_vertices == 1000000);
        const auto rejected = FU::analyze(result, prepared, core, limited);
        INFO(rejected.error);
        CHECK(rejected.status == FU::Status::ResourceLimit);
        CHECK(rejected.dense_requests.empty());
        CHECK(rejected.regions.empty());
        CHECK(result.manufacturing_dependencies.operations.empty());
    }
    FU::Options excessive;
    excessive.max_request_vertices = 2049;
    CHECK(FU::analyze(result, prepared, core, excessive).status == FU::Status::InvalidInput);
    excessive                            = {};
    excessive.max_total_request_vertices = 65537;
    CHECK(FU::analyze(result, prepared, core, excessive).status == FU::Status::InvalidInput);
}
