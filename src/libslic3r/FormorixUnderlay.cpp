#include "FormorixUnderlay.hpp"
#include "FormorixCorePreview.hpp"
#include "FormorixGeometry.hpp"
#include "FormorixHybrid.hpp"
#include "FormorixProcess.hpp"
#include "FormorixSurfaces.hpp"
#include "ClipperUtils.hpp"
#include "ExPolygon.hpp"
#include "Polygon.hpp"
#include "Point.hpp"
#include "I18N.hpp"
#include "libslic3r.h"
#include <Eigen/Geometry>
#include <algorithm>
#include <exception>
#include <functional>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::FormorixUnderlay {
namespace {
namespace FH              = FormorixHybrid;
namespace CP              = FormorixCorePreview;
namespace PS              = FormorixProcess;
namespace FS              = FormorixSurfaces;
constexpr double infinity = std::numeric_limits<double>::infinity();
struct Failure : std::runtime_error
{
    Status status;
    Failure(Status value, const std::string& message) : std::runtime_error(message), status(value) {}
};
void check(const std::function<bool()>& canceled)
{
    if (canceled && canceled())
        throw Failure(Status::Canceled, _u8L("Underlay analysis canceled"));
}
void validate(const Options& o)
{
    if (!std::isfinite(o.max_sample_edge) || o.max_sample_edge <= 0. || !std::isfinite(o.search_distance) || o.search_distance < .05 ||
        o.search_distance > 100. || o.max_samples == 0 || o.max_faces == 0 || o.max_subdivisions == 0 || o.max_layer_candidates == 0 ||
        o.max_polygon_vertices == 0 || o.max_requests == 0 || o.max_requests > 256 || o.max_edge_tests == 0 ||
        o.max_request_vertices == 0 || o.max_request_vertices > 2048 || o.max_total_request_vertices == 0 ||
        o.max_total_request_vertices > 65536)
        throw Failure(Status::InvalidInput, _u8L("Invalid underlay sampling or resource limits"));
}
void warn(std::vector<std::string>& warnings, const std::string& text)
{
    if (std::find(warnings.begin(), warnings.end(), text) == warnings.end())
        warnings.push_back(text);
}
void invalidate(FH::Result& result)
{
    result.manufacturing_dependencies = {};
    result.missing_underlay.clear();
    result.missing_core.clear();
    result.unsuitable_infill.clear();
    result.path_gaps.clear();
    result.unknown_underlay.clear();
    result.native_layer_gaps.clear();
    for (auto& region : result.regions) {
        region.interface_samples.clear();
        region.interface_normals.clear();
        region.underlay_contacts.clear();
        region.underlay = {};
    }
}
void clear_layer_evidence(FH::Result& result)
{
    result.manufacturing_dependencies = {};
    result.native_layer_gaps.clear();
    result.path_gaps.clear();
    for (auto& region : result.regions) {
        region.underlay.native_layer_gaps = 0;
        for (auto& contact : region.underlay_contacts) {
            if (contact.kind == FH::UnderlayKind::NativeLayerGap) {
                contact.kind = FH::UnderlayKind::PathGap;
                ++region.underlay.path_gaps;
            }
            if (contact.kind == FH::UnderlayKind::PathGap && result.path_gaps.size() < 10000)
                result.path_gaps.push_back(contact.position);
        }
    }
}
struct Leaf
{
    std::array<Vec3d, 3> vertices;
    std::array<Vec3d, 3> barycentric;
};
double cross(const Vec2d& a, const Vec2d& b) { return a.x() * b.y() - a.y() * b.x(); }
struct Ring
{
    std::vector<Vec2d> vertices;
};
struct Polygon2
{
    Ring contour;
    std::vector<Ring> holes;
};
struct Layer
{
    std::size_t source = 0;
    std::vector<Polygon2> polygons;
    Vec2d minimum = Vec2d::Constant(infinity), maximum = Vec2d::Constant(-infinity);
    double bottom = 0., top = 0.;
};
enum class Location { Outside, Inside, Boundary };
struct QueryWork
{
    Analysis& out;
    const Options& options;
    const std::function<bool()>& canceled;
    void edge()
    {
        if (++out.edge_tests > options.max_edge_tests)
            throw Failure(Status::ResourceLimit, _u8L("Underlay polygon query budget exceeded"));
        if ((out.edge_tests & 255u) == 0)
            check(canceled);
    }
};
Location locate(const Ring& ring, const Vec2d& point, QueryWork& work)
{
    bool inside = false;
    for (std::size_t i = 0; i < ring.vertices.size(); ++i) {
        work.edge();
        const auto& a = ring.vertices[i];
        const auto& b = ring.vertices[(i + 1) % ring.vertices.size()];
        if (cross(b - a, point - a) == 0. && point.x() >= std::min(a.x(), b.x()) && point.x() <= std::max(a.x(), b.x()) &&
            point.y() >= std::min(a.y(), b.y()) && point.y() <= std::max(a.y(), b.y()))
            return Location::Boundary;
        if ((a.y() > point.y()) != (b.y() > point.y())) {
            const double x = a.x() + (point.y() - a.y()) * (b.x() - a.x()) / (b.y() - a.y());
            if (point.x() < x)
                inside = !inside;
        }
    }
    return inside ? Location::Inside : Location::Outside;
}
bool contains(const Polygon2& polygon, const Vec2d& point, QueryWork& work)
{
    const auto outer = locate(polygon.contour, point, work);
    if (outer == Location::Outside)
        return false;
    if (outer == Location::Boundary)
        return true;
    for (const auto& hole : polygon.holes) {
        const auto inner = locate(hole, point, work);
        if (inner == Location::Boundary)
            return true;
        if (inner == Location::Inside)
            return false;
    }
    return true;
}
std::vector<Layer> layers(const CP::Result& source, const Options& o, const std::function<bool()>& canceled)
{
    std::vector<Layer> result;
    std::size_t vertices = 0;
    for (std::size_t i = 0; i < source.layers.size(); ++i) {
        check(canceled);
        const auto& s = source.layers[i];
        if (!std::isfinite(s.slice_z) || !std::isfinite(s.bottom_z) || !std::isfinite(s.top_z) || !std::isfinite(s.print_bottom_z) ||
            !std::isfinite(s.print_top_z) || s.bottom_z >= s.top_z || s.print_bottom_z >= s.print_top_z || s.slice_z < s.bottom_z ||
            s.slice_z > s.top_z)
            throw Failure(Status::InvalidInput, _u8L("Invalid native layer coordinates for underlay analysis"));
        Layer out;
        out.source   = i;
        out.bottom   = s.print_bottom_z;
        out.top      = s.print_top_z;
        auto convert = [&](const Polygon& p) {
            if (p.points.size() < 3)
                throw Failure(Status::InvalidInput, _u8L("Invalid native layer polygon"));
            if (p.points.size() > o.max_polygon_vertices - vertices)
                throw Failure(Status::ResourceLimit, _u8L("Underlay native polygon budget exceeded"));
            vertices += p.points.size();
            Ring r;
            for (const auto& point : p.points) {
                const Vec2d q = unscaled<double>(point);
                if (!q.allFinite() || q.cwiseAbs().maxCoeff() > 1e7)
                    throw Failure(Status::InvalidInput, _u8L("Invalid native layer polygon coordinate"));
                r.vertices.push_back(q);
                out.minimum = out.minimum.cwiseMin(q);
                out.maximum = out.maximum.cwiseMax(q);
            }
            return r;
        };
        for (const auto& p : s.area) {
            Polygon2 polygon;
            polygon.contour = convert(p.contour);
            for (const auto& hole : p.holes)
                polygon.holes.push_back(convert(hole));
            out.polygons.push_back(std::move(polygon));
        }
        result.push_back(std::move(out));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.top < b.top; });
    // One conventional core's native slabs are ordered. Do not silently use the
    // sorted-top acceleration for a future interleaved or nested layer model.
    for (std::size_t i = 1; i < result.size(); ++i)
        if (result[i].bottom < result[i - 1].bottom)
            throw Failure(Status::InvalidInput, _u8L("Interleaved native layer intervals require a separate underlay model"));
    return result;
}
double prism_hit(const Layer& layer, const Vec3d& q, const Vec3d& direction, double search, QueryWork& work)
{
    double first = 0., last = search;
    if (direction.z() == 0.) {
        if (q.z() < layer.bottom || q.z() > layer.top)
            return infinity;
    } else {
        double a = (layer.bottom - q.z()) / direction.z(), b = (layer.top - q.z()) / direction.z();
        if (a > b)
            std::swap(a, b);
        first = std::max(first, a);
        last  = std::min(last, b);
        if (first > last)
            return infinity;
    }
    const Vec2d origin(q.x(), q.y()), delta(direction.x(), direction.y());
    double nearest = infinity;
    for (const auto& polygon : layer.polygons) {
        if (contains(polygon, origin + first * delta, work)) {
            nearest = std::min(nearest, first);
            continue;
        }
        std::vector<double> events{first, last};
        auto crossings = [&](const Ring& ring) {
            for (std::size_t i = 0; i < ring.vertices.size(); ++i) {
                work.edge();
                const auto& a            = ring.vertices[i];
                const Vec2d edge         = ring.vertices[(i + 1) % ring.vertices.size()] - a;
                const double denominator = cross(delta, edge);
                if (denominator != 0.) {
                    const double t = cross(a - origin, edge) / denominator;
                    const double u = cross(a - origin, delta) / denominator;
                    if (t >= first && t <= last && u >= 0. && u <= 1.)
                        events.push_back(t);
                }
            }
        };
        crossings(polygon.contour);
        for (const auto& hole : polygon.holes)
            crossings(hole);
        std::sort(events.begin(), events.end());
        for (std::size_t i = 0; i < events.size(); ++i) {
            if (contains(polygon, origin + events[i] * delta, work) ||
                (i + 1 < events.size() && contains(polygon, origin + (.5 * (events[i] + events[i + 1])) * delta, work))) {
                nearest = std::min(nearest, events[i]);
                break;
            }
        }
    }
    return nearest;
}
Polygon footprint(const Probe& probe, const Vec3d& displacement, double radius)
{
    Vec2d low = Vec2d::Constant(infinity), high = Vec2d::Constant(-infinity);
    for (const auto& p : probe.footprint) {
        const Vec2d xy(p.x() + displacement.x(), p.y() + displacement.y());
        low  = low.cwiseMin(xy);
        high = high.cwiseMax(xy);
    }
    low.array() -= radius;
    high.array() += radius;
    // A conservative, finite local request footprint, not a contact proof.
    // It remains nonzero for vertical faces. Native polygons clip it afterwards.
    return Polygon(Points{Point::new_scale(low.x(), low.y()), Point::new_scale(high.x(), low.y()), Point::new_scale(high.x(), high.y()),
                          Point::new_scale(low.x(), high.y())});
}
} // namespace

Preparation prepare(FH::Result& result, const Options& options, const std::function<bool()>& canceled)
{
    Preparation out;
    try {
        validate(options);
        check(canceled);
        if (result.regions.empty())
            throw Failure(Status::InvalidInput, _u8L("Underlay preparation requires a surface region"));
        std::map<int, std::size_t> ids;
        for (const auto& r : result.regions) {
            if (r.id <= 0 || !ids.emplace(r.id, out.regions.size()).second)
                throw Failure(Status::InvalidInput, _u8L("Invalid or duplicate underlay region"));
            PreparedRegion prepared;
            prepared.region = r.id;
            out.regions.push_back(std::move(prepared));
        }
        std::size_t faces = 0, count = 0, work = 0;
        for (std::size_t i = 0; i < result.partition.interfaces.size(); ++i) {
            check(canceled);
            const auto& interface = result.partition.interfaces[i];
            if (interface.first != 0 && interface.second != 0)
                continue;
            const int region = interface.first == 0 ? interface.second : interface.first;
            const auto found = ids.find(region);
            if (found == ids.end())
                continue;
            if (result.core.faces.empty())
                throw Failure(Status::InvalidInput, _u8L("A core interface exists without a conventional core"));
            auto& prepared = out.regions[found->second];
            if (interface.mesh.faces.size() > options.max_faces - faces)
                throw Failure(Status::ResourceLimit, _u8L("Underlay interface face budget exceeded"));
            faces += interface.mesh.faces.size();
            for (std::size_t f = 0; f < interface.mesh.faces.size(); ++f) {
                const auto& face = interface.mesh.faces[f];
                Leaf initial;
                for (int k = 0; k < 3; ++k) {
                    if (face[k] < 0 || std::size_t(face[k]) >= interface.mesh.vertices.size())
                        throw Failure(Status::InvalidInput, _u8L("Invalid underlay interface triangle"));
                    initial.vertices[k] = interface.mesh.vertices[face[k]];
                    if (!initial.vertices[k].allFinite() || initial.vertices[k].cwiseAbs().maxCoeff() > 1e7)
                        throw Failure(Status::InvalidInput, _u8L("Invalid underlay interface coordinate"));
                    initial.barycentric[k] = Vec3d::Unit(k);
                }
                Vec3d normal      = (initial.vertices[1] - initial.vertices[0]).cross(initial.vertices[2] - initial.vertices[0]);
                const double norm = normal.norm();
                if (!std::isfinite(norm) || norm <= 0.)
                    throw Failure(Status::InvalidInput, _u8L("Degenerate underlay interface triangle"));
                normal /= norm;
                if (interface.first != 0)
                    normal = -normal;
                prepared.interface_area += norm / 2.;
                std::vector<Leaf> pending{initial};
                while (!pending.empty()) {
                    check(canceled);
                    if (++work > options.max_subdivisions)
                        throw Failure(Status::ResourceLimit, _u8L("Underlay subdivision budget exceeded"));
                    Leaf leaf = std::move(pending.back());
                    pending.pop_back();
                    int edge      = 0;
                    double length = 0.;
                    for (int k = 0; k < 3; ++k) {
                        const double candidate = (leaf.vertices[(k + 1) % 3] - leaf.vertices[k]).squaredNorm();
                        if (candidate > length) {
                            length = candidate;
                            edge   = k;
                        }
                    }
                    if (length > options.max_sample_edge * options.max_sample_edge) {
                        const int next          = (edge + 1) % 3;
                        const Vec3d midpoint    = .5 * (leaf.vertices[edge] + leaf.vertices[next]);
                        const Vec3d barycentric = .5 * (leaf.barycentric[edge] + leaf.barycentric[next]);
                        Leaf other              = leaf;
                        leaf.vertices[edge]     = midpoint;
                        leaf.barycentric[edge]  = barycentric;
                        other.vertices[next]    = midpoint;
                        other.barycentric[next] = barycentric;
                        pending.push_back(std::move(leaf));
                        pending.push_back(std::move(other));
                        continue;
                    }
                    if (++count > options.max_samples)
                        throw Failure(Status::ResourceLimit, _u8L("Underlay sample budget exceeded"));
                    Probe probe;
                    probe.source_interface = i;
                    probe.source_face      = f;
                    probe.footprint        = leaf.vertices;
                    probe.position         = (leaf.vertices[0] + leaf.vertices[1] + leaf.vertices[2]) / 3.;
                    const Vec3d bary       = (leaf.barycentric[0] + leaf.barycentric[1] + leaf.barycentric[2]) / 3.;
                    for (int k = 0; k < 3; ++k)
                        probe.barycentric[k] = bary[k];
                    probe.outward = normal;
                    probe.area    = .5 * (leaf.vertices[1] - leaf.vertices[0]).cross(leaf.vertices[2] - leaf.vertices[0]).norm();
                    if (!std::isfinite(probe.area) || probe.area <= 0.)
                        throw Failure(Status::InvalidInput, _u8L("Underlay footprint is not numerically representable"));
                    prepared.probes.push_back(std::move(probe));
                }
            }
        }
        check(canceled);
        invalidate(result);
        for (std::size_t i = 0; i < out.regions.size(); ++i) {
            auto& prepared                     = out.regions[i];
            auto& region                       = result.regions[i];
            prepared.external_support_required = prepared.probes.empty();
            for (const auto& p : prepared.probes) {
                region.interface_samples.push_back(p.position);
                region.interface_normals.push_back(p.outward);
            }
            if (prepared.external_support_required)
                warn(result.warnings, _u8L("A 5D region has no core interface: an unplanned external supporting structure is required"));
        }
        out.status = Status::Complete;
    } catch (const Failure& e) {
        invalidate(result);
        out.regions.clear();
        out.status = e.status;
        out.error  = e.what();
    } catch (const std::exception& e) {
        invalidate(result);
        out.regions.clear();
        out.error = e.what();
    }
    return out;
}

Analysis analyze(FH::Result& result,
                 const Preparation& prepared,
                 const CP::Result& conventional,
                 const Options& options,
                 const std::function<bool()>& canceled)
{
    Analysis out;
    clear_layer_evidence(result);
    try {
        validate(options);
        check(canceled);
        if (!prepared.ok() || prepared.regions.size() != result.regions.size() || !conventional.error.empty())
            throw Failure(Status::InvalidInput, _u8L("Underlay analysis requires matching interface samples and native layer data"));
        const auto native = layers(conventional, options, canceled);
        QueryWork work{out, options, canceled};
        std::map<std::pair<int, std::size_t>, Polygons> footprints;
        std::size_t footprint_vertices = 0;
        for (std::size_t r = 0; r < prepared.regions.size(); ++r) {
            check(canceled);
            const auto& geometry = prepared.regions[r];
            const auto& region   = result.regions[r];
            if (geometry.region != region.id || geometry.probes.size() != region.underlay_contacts.size() ||
                !std::isfinite(region.process.layer_height) || region.process.layer_height <= 0. ||
                !std::isfinite(region.process.line_width) || region.process.line_width <= 0. ||
                !std::isfinite(region.settings.underlay_density) || region.settings.underlay_density < 0. ||
                region.settings.underlay_density > 100.)
                throw Failure(Status::InvalidInput, _u8L("Underlay contacts or local process settings do not match their prepared region"));
            RegionEvidence evidence;
            evidence.region                    = region.id;
            evidence.interface_area            = geometry.interface_area;
            evidence.external_support_required = geometry.external_support_required;
            const double allowed               = std::min(.05, region.process.layer_height * .25);
            for (std::size_t i = 0; i < geometry.probes.size(); ++i) {
                check(canceled);
                const auto& probe   = geometry.probes[i];
                const auto& contact = region.underlay_contacts[i];
                if ((contact.position - probe.position).norm() > 1e-9 || (contact.inward + probe.outward).norm() > 1e-9 ||
                    !contact.position.allFinite() || !contact.inward.allFinite() || std::isnan(contact.raw_gap) || contact.raw_gap < 0.)
                    throw Failure(Status::InvalidInput, _u8L("Stale or invalid underlay contact metadata"));
                SampleEvidence sample;
                sample.probe   = i;
                sample.raw_gap = contact.raw_gap;
                if (contact.raw_geometry_unknown || contact.kind == FH::UnderlayKind::MissingCore || native.empty()) {
                    ++evidence.unknown;
                    evidence.unknown_area += probe.area;
                } else if (contact.raw_gap <= allowed) {
                    sample.kind = RawKind::Contact;
                } else {
                    evidence.raw_missing_area += probe.area;
                    const Vec3d end  = contact.position + options.search_distance * contact.inward;
                    const double low = std::min(contact.position.z(), end.z()), high = std::max(contact.position.z(), end.z());
                    auto begin          = std::lower_bound(native.begin(), native.end(), low,
                                                           [](const auto& layer, double z) { return layer.top < z; });
                    const Vec2d xy_low  = contact.position.head<2>().cwiseMin(end.head<2>());
                    const Vec2d xy_high = contact.position.head<2>().cwiseMax(end.head<2>());
                    for (auto it = begin; it != native.end() && it->bottom <= high; ++it) {
                        check(canceled);
                        if (++out.layer_candidates > options.max_layer_candidates)
                            throw Failure(Status::ResourceLimit, _u8L("Underlay native layer query budget exceeded"));
                        if ((xy_high.array() < it->minimum.array()).any() || (xy_low.array() > it->maximum.array()).any())
                            continue;
                        const double hit = prism_hit(*it, contact.position, contact.inward, options.search_distance, work);
                        if (hit < sample.native_prism_gap) {
                            sample.native_prism_gap = hit;
                            sample.layer            = it->source;
                        }
                    }
                    const double numeric = 8. * SCALING_FACTOR + 128. * std::numeric_limits<double>::epsilon() *
                                                                     std::max(1., probe.position.cwiseAbs().maxCoeff());
                    if (sample.native_prism_gap > allowed + numeric) {
                        sample.kind = RawKind::NativeLayerGap;
                        ++evidence.native_layer_gaps;
                        evidence.native_layer_gap_area += probe.area;
                    } else if (sample.native_prism_gap < allowed - numeric) {
                        sample.kind = RawKind::PathGap;
                        ++evidence.path_gaps;
                        evidence.path_gap_area += probe.area;
                        if (region.settings.dense_underlay && region.settings.underlay_density > 0. &&
                            contact.kind != FH::UnderlayKind::MissingCore) {
                            if (4 > options.max_polygon_vertices - footprint_vertices)
                                throw Failure(Status::ResourceLimit, _u8L("Underlay footprint budget exceeded"));
                            footprint_vertices += 4;
                            footprints[{region.id, sample.layer}].push_back(
                                footprint(probe, sample.native_prism_gap * contact.inward, .5 * region.process.line_width));
                        }
                    } else {
                        ++evidence.unknown;
                        evidence.unknown_area += probe.area;
                    }
                }
                evidence.samples.push_back(sample);
            }
            out.regions.push_back(std::move(evidence));
        }
        std::size_t request_vertices = 0;
        for (auto& entry : footprints) {
            check(canceled);
            if (out.dense_requests.size() >= options.max_requests)
                throw Failure(Status::ResourceLimit, _u8L("Underlay local modifier request budget exceeded"));
            const auto& source = conventional.layers.at(entry.first.second);
            const auto found   = std::find_if(result.regions.begin(), result.regions.end(),
                                              [&](const auto& r) { return r.id == entry.first.first; });
            CP::DenseRequest request;
            request.region = entry.first.first;
            request.area   = intersection_ex(union_ex(entry.second), source.area);
            check(canceled);
            if (request.area.empty())
                continue;
            std::size_t local_vertices = 0;
            for (const auto& p : request.area) {
                auto count = [&](const Polygon& ring) {
                    if (ring.points.size() > options.max_request_vertices - local_vertices)
                        throw Failure(Status::ResourceLimit, _u8L("Underlay polygon exceeds the per-request native modifier budget"));
                    if (ring.points.size() > options.max_total_request_vertices - request_vertices)
                        throw Failure(Status::ResourceLimit, _u8L("Underlay polygons exceed the combined native modifier budget"));
                    local_vertices += ring.points.size();
                    request_vertices += ring.points.size();
                };
                count(p.contour);
                for (const auto& hole : p.holes)
                    count(hole);
            }
            request.bottom_z = source.bottom_z;
            request.top_z    = source.top_z;
            request.slice_z  = source.slice_z;
            request.density  = found->settings.underlay_density;
            out.dense_requests.push_back(std::move(request));
        }
        PS::Dependencies dependencies;
        const auto core_path = std::find_if(conventional.paths.begin(), conventional.paths.end(), [](const auto& p) { return !p.support; });
        const bool assigned_core        = core_path != conventional.paths.end() && !core_path->process.material_id.empty();
        const int core_head             = assigned_core ? core_path->head : -1;
        const std::string core_material = assigned_core ? core_path->process.material_id : "";
        int next                        = 1;
        for (std::size_t i = 0; i < out.regions.size(); ++i) {
            const auto& evidence = out.regions[i];
            const auto& region   = result.regions[i];
            int prerequisite     = next++;
            PS::Operation operation;
            operation.id       = prerequisite;
            operation.region   = region.id;
            operation.head     = core_head;
            operation.material = core_material;
            operation.assigned = assigned_core;
            operation.kind     = PS::OperationKind::Core;
            if (evidence.external_support_required) {
                operation.head = -1;
                operation.material.clear();
                operation.assigned = false;
                operation.kind     = region.settings.support_material == FS::SupportMaterial::Soluble   ? PS::OperationKind::Soluble :
                                     region.settings.support_material == FS::SupportMaterial::Breakaway ? PS::OperationKind::Breakaway :
                                                                                                          PS::OperationKind::Solid;
            }
            dependencies.operations.push_back(operation);
            if (std::any_of(out.dense_requests.begin(), out.dense_requests.end(), [&](const auto& d) { return d.region == region.id; }) ||
                std::find(conventional.applied_dense_regions.begin(), conventional.applied_dense_regions.end(), region.id) !=
                    conventional.applied_dense_regions.end()) {
                const int dense = next++;
                operation.id    = dense;
                operation.kind  = PS::OperationKind::DenseTransition;
                dependencies.operations.push_back(operation);
                dependencies.edges.push_back({prerequisite, dense});
                prerequisite = dense;
            }
            operation.id       = next++;
            operation.head     = region.head;
            operation.material = region.process.material_id;
            operation.assigned = !operation.material.empty();
            if (!operation.assigned)
                operation.head = -1;
            operation.kind = PS::OperationKind::Surface;
            dependencies.operations.push_back(operation);
            dependencies.edges.push_back({prerequisite, operation.id});
        }
        if (!dependencies.valid())
            throw Failure(Status::InvalidInput, _u8L("Invalid underlay manufacturing dependency identities"));
        check(canceled);
        result.native_layer_gaps.clear();
        for (std::size_t i = 0; i < out.regions.size(); ++i) {
            auto& region                      = result.regions[i];
            region.underlay.native_layer_gaps = 0;
            region.underlay.coverage_bounded  = false;
            for (const auto& sample : out.regions[i].samples) {
                auto& contact = region.underlay_contacts[sample.probe];
                // Raw classification never promotes nonunit-Flow/unsupported
                // material geometry to suitable contact or actionable infill.
                if (sample.kind == RawKind::NativeLayerGap) {
                    ++region.underlay.native_layer_gaps;
                    if (result.native_layer_gaps.size() < 10000)
                        result.native_layer_gaps.push_back(contact.position);
                    if (contact.kind == FH::UnderlayKind::PathGap) {
                        contact.kind = FH::UnderlayKind::NativeLayerGap;
                        if (region.underlay.path_gaps > 0)
                            --region.underlay.path_gaps;
                    }
                }
            }
        }
        result.path_gaps.clear();
        for (const auto& r : result.regions)
            for (const auto& c : r.underlay_contacts)
                if (c.kind == FH::UnderlayKind::PathGap && result.path_gaps.size() < 10000)
                    result.path_gaps.push_back(c.position);
        result.manufacturing_dependencies = std::move(dependencies);
        warn(out.warnings,
             _u8L("Underlay evidence is finite area-weighted sampling, not complete footprint, adhesion or strength verification"));
        if (!out.dense_requests.empty())
            warn(out.warnings,
                 _u8L("Local dense underlay requests are unplanned; only regenerated native paths can establish new contact evidence"));
        for (const auto& warning : out.warnings)
            warn(result.warnings, warning);
        out.status = Status::Complete;
    } catch (const Failure& e) {
        result.manufacturing_dependencies = {};
        out.regions.clear();
        out.dense_requests.clear();
        out.status = e.status;
        out.error  = e.what();
    } catch (const std::exception& e) {
        result.manufacturing_dependencies = {};
        out.regions.clear();
        out.dense_requests.clear();
        out.error = e.what();
    }
    return out;
}
} // namespace Slic3r::FormorixUnderlay
