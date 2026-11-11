#include "FormorixSurfacePaths.hpp"
#include "FormorixSurfacePathsInternal.hpp"
#include "FormorixGeometry.hpp"
#include "Point.hpp"
#include "I18N.hpp"
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Surface_mesh/Surface_mesh.h>
#include <CGAL/Lazy.h>
#include <CGAL/global_functions_distance_3.h>
#include <CGAL/Polygon_mesh_slicer.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits_3.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/Polygon_mesh_processing/locate.h>
#include <CGAL/Polygon_mesh_processing/manifoldness.h>
#include <CGAL/Polygon_mesh_processing/self_intersections.h>
#include <CGAL/Kernel/global_functions_3.h>
#include <CGAL/number_utils.h>
#include <CGAL/squared_distance_3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace Slic3r::FormorixSurfacePaths::detail {
namespace {
using K           = CGAL::Exact_predicates_exact_constructions_kernel;
using Mesh        = CGAL::Surface_mesh<K::Point_3>;
using Point       = K::Point_3;
using Vector      = K::Vector_3;
using FT          = K::FT;
using Barycentric = std::array<FT, 3>;
namespace PMP     = CGAL::Polygon_mesh_processing;
struct Interval
{
    double lower, upper;
};
double up_add(double a, double b) { return a == 0. && b == 0. ? 0. : std::nextafter(a + b, INFINITY); }
double up_multiply(double a, double b) { return a == 0. || b == 0. ? 0. : std::nextafter(a * b, INFINITY); }
double up_divide(double a, double b) { return a == 0. ? 0. : std::nextafter(a / b, INFINITY); }
Interval root_interval(const FT& square)
{
    if (square == 0)
        return {0., 0.};
    const auto value = CGAL::to_interval(square);
    return {std::max(0., std::nextafter(std::sqrt(std::max(0., value.first)), -INFINITY)),
            std::nextafter(std::sqrt(std::max(0., value.second)), INFINITY)};
}
Point eager(const Point& p) { return Point(FT(CGAL::exact(p.x())), FT(CGAL::exact(p.y())), FT(CGAL::exact(p.z()))); }
Vector eager(const Vector& p) { return Vector(FT(CGAL::exact(p.x())), FT(CGAL::exact(p.y())), FT(CGAL::exact(p.z()))); }
struct State
{
    Point point;
    Vector normal;
    Barycentric barycentric;
};
State midpoint(const State& a, const State& b)
{
    State result{eager(CGAL::midpoint(a.point, b.point)), eager((a.normal + b.normal) / FT(2)), {}};
    for (int i = 0; i < 3; ++i)
        result.barycentric[i] = FT(CGAL::exact((a.barycentric[i] + b.barycentric[i]) / FT(2)));
    return result;
}
struct Converted
{
    Knot knot;
    double position_error = 0., normal_error = 0.;
};
Converted convert(const State& state)
{
    Converted result;
    for (int k = 0; k < 3; ++k)
        result.knot.position[k] = CGAL::to_double(state.point[k]);
    const Point rounded(result.knot.position.x(), result.knot.position.y(), result.knot.position.z());
    result.position_error = root_interval(CGAL::squared_distance(state.point, rounded)).upper;
    int nonzero = 0, coordinate = 0;
    for (int k = 0; k < 3; ++k)
        if (state.normal[k] != 0) {
            ++nonzero;
            coordinate = k;
        }
    if (nonzero == 0)
        throw Failure(Status::InvalidInput, _u8L("The interpolated surface normal vanishes"));
    if (nonzero == 1) {
        result.knot.normal             = Vec3d::Zero();
        result.knot.normal[coordinate] = state.normal[coordinate] > 0 ? 1. : -1.;
        return result;
    }
    const auto length = root_interval(state.normal.squared_length());
    if (!(length.lower > 0.) || !std::isfinite(length.upper))
        throw Failure(Status::InvalidInput, _u8L("The surface normal cannot be normalized with a finite bound"));
    const double nominal_length = std::sqrt(CGAL::to_double(state.normal.squared_length()));
    for (int k = 0; k < 3; ++k) {
        result.knot.normal[k] = CGAL::to_double(state.normal[k]) / nominal_length;
        const auto component  = CGAL::to_interval(state.normal[k]);
        const std::array<double, 4> quotients{{component.first / length.lower, component.first / length.upper,
                                               component.second / length.lower, component.second / length.upper}};
        const double lower  = std::nextafter(*std::min_element(quotients.begin(), quotients.end()), -INFINITY);
        const double upper  = std::nextafter(*std::max_element(quotients.begin(), quotients.end()), INFINITY);
        const double error  = std::max(std::nextafter(std::abs(result.knot.normal[k] - lower), INFINITY),
                                       std::nextafter(std::abs(result.knot.normal[k] - upper), INFINITY));
        result.normal_error = up_add(result.normal_error, error); // L1 bounds Euclidean normal error
    }
    if (!result.knot.position.allFinite() || !result.knot.normal.allFinite() || !std::isfinite(result.normal_error))
        throw Failure(Status::Failure, _u8L("Surface contour output cannot be represented in double precision"));
    return result;
}
double interpolation_error(const State& a, const State& b, double distance)
{
    const Vector delta = b.normal - a.normal;
    const FT delta2    = delta.squared_length();
    if (delta2 == 0)
        return 0.;
    FT t                       = -(a.normal * delta) / delta2;
    t                          = std::max(FT(0), std::min(FT(1), t));
    const FT minimum2          = (a.normal + delta * t).squared_length();
    const double minimum_lower = CGAL::to_interval(minimum2).first;
    const double delta_upper   = CGAL::to_interval(delta2).second;
    if (!(minimum_lower > 0.))
        throw Failure(Status::InvalidInput, _u8L("Opposing surface normals make the offset interpolation undefined"));
    if (distance == 0.)
        return 0.;
    // u(t) is affine on this carrier. For n=u/|u|, |n''| <=
    // 6*|u'|^2/min|u|^2 over the COMPLETE interval. Linear chord error <= M/8.
    return up_divide(up_multiply(distance, up_divide(up_multiply(6., delta_upper), minimum_lower)), 8.);
}
std::array<double, 3> to_double(const Barycentric& barycentric)
{ return {{CGAL::to_double(barycentric[0]), CGAL::to_double(barycentric[1]), CGAL::to_double(barycentric[2])}}; }
} // namespace

Result trace_exact(const Input& input, const Options& options, const std::function<bool()>& canceled)
{
    auto check = [&] {
        if (canceled && canceled())
            throw Failure(Status::Canceled, _u8L("Surface contour generation canceled"));
    };
    Mesh mesh;
    std::map<std::tuple<double, double, double>, std::pair<Mesh::Vertex_index, std::size_t>> unique;
    std::vector<Mesh::Vertex_index> indices;
    indices.reserve(input.surface.vertices.size());
    for (std::size_t i = 0; i < input.surface.vertices.size(); ++i) {
        check();
        const auto& p = input.surface.vertices[i];
        if (!p.allFinite() || p.cwiseAbs().maxCoeff() > 1e7)
            throw Failure(Status::InvalidInput, _u8L("Invalid surface contour mesh coordinate"));
        const auto key   = std::make_tuple(p.x(), p.y(), p.z());
        const auto found = unique.find(key);
        if (found != unique.end()) {
            if (!(input.vertex_normals[i].array() == input.vertex_normals[found->second.second].array()).all())
                throw Failure(Status::InvalidInput, _u8L("Coincident source vertices have inconsistent surface normal fields"));
            indices.push_back(found->second.first);
        } else {
            const auto vertex = mesh.add_vertex(Point(p.x(), p.y(), p.z()));
            unique.emplace(key, std::make_pair(vertex, i));
            indices.push_back(vertex);
        }
    }
    std::map<Mesh::Face_index, int> source_ids;
    for (std::size_t i = 0; i < input.surface.faces.size(); ++i) {
        check();
        const auto& f = input.surface.faces[i];
        for (int index : f)
            if (index < 0 || std::size_t(index) >= indices.size())
                throw Failure(Status::InvalidInput, _u8L("Invalid surface contour triangle index"));
        const auto a = indices[f[0]], b = indices[f[1]], c = indices[f[2]];
        if (a == b || b == c || c == a || CGAL::collinear(mesh.point(a), mesh.point(b), mesh.point(c)))
            throw Failure(Status::InvalidMesh, _u8L("A degenerate source triangle prevents exact surface contours"));
        const auto face = mesh.add_face(a, b, c);
        if (face == Mesh::null_face())
            throw Failure(Status::InvalidMesh, _u8L("The surface contour mesh is nonmanifold or inconsistently oriented"));
        source_ids.emplace(face, int(i));
    }
    if (!mesh.is_valid(false) || !CGAL::is_valid_polygon_mesh(mesh) || !CGAL::is_triangle_mesh(mesh))
        throw Failure(Status::InvalidMesh, _u8L("Invalid triangulated surface graph"));
    std::vector<Mesh::Halfedge_index> nonmanifold;
    PMP::non_manifold_vertices(mesh, std::back_inserter(nonmanifold));
    if (!nonmanifold.empty() || PMP::does_self_intersect(mesh))
        throw Failure(Status::InvalidMesh, _u8L("The source surface contains a nonmanifold vertex or a self-intersection"));
    check();
    using Primitive = CGAL::AABB_face_graph_triangle_primitive<Mesh>;
    using Traits    = CGAL::AABB_traits_3<K, Primitive>;
    CGAL::AABB_tree<Traits> faces(mesh.faces().begin(), mesh.faces().end(), mesh);
    faces.build();
    CGAL::Polygon_mesh_slicer<Mesh, K> slicer(mesh);
    const Vec3d plane_normal = options.plane_normal.normalized();
    Result out;
    out.position_rounding_bound = out.normal_rounding_bound = out.offset_interpolation_bound = 0.;
    std::size_t knots                                                                        = 0;
    bool coplanar                                                                            = false;
    auto state_on_face                                                                       = [&](const Point& p, int face) {
        const auto& f = input.surface.faces[face];
        std::array<Point, 3> triangle;
        for (int k = 0; k < 3; ++k) {
            const auto& v = input.surface.vertices[f[k]];
            triangle[k]   = Point(v.x(), v.y(), v.z());
        }
        const auto bary = PMP::barycentric_coordinates(triangle[0], triangle[1], triangle[2], p);
        if (bary[0] < 0 || bary[1] < 0 || bary[2] < 0 || bary[0] + bary[1] + bary[2] != 1)
            throw Failure(Status::Failure, _u8L("An exact contour point leaves its source triangle carrier"));
        State state{eager(p), Vector(0, 0, 0), {}};
        for (int k = 0; k < 3; ++k) {
            state.barycentric[k] = FT(CGAL::exact(bary[k]));
            const auto& n        = input.vertex_normals[f[k]];
            state.normal         = state.normal + Vector(n.x(), n.y(), n.z()) * bary[k];
        }
        state.normal = eager(state.normal);
        return state;
    };
    for (std::size_t plane_index = 0; plane_index < options.plane_offsets.size(); ++plane_index) {
        check();
        const K::Plane_3 plane(plane_normal.x(), plane_normal.y(), plane_normal.z(), -options.plane_offsets[plane_index]);
        std::vector<std::vector<Point>> contours;
        slicer(plane, std::back_inserter(contours));
        check();
        for (const auto& contour : contours) {
            check();
            if (contour.size() < 2) {
                ++out.isolated_contacts;
                continue;
            }
            Run run;
            run.plane_index = plane_index;
            run.closed      = contour.front() == contour.back();
            if (run.closed && contour.size() < 4)
                throw Failure(Status::InvalidMesh, _u8L("A closed contour has fewer than three distinct points"));
            std::set<Point, K::Less_xyz_3> distinct;
            const std::size_t unique_count = contour.size() - (run.closed ? 1 : 0);
            if (unique_count > options.max_knots - std::min(knots, options.max_knots))
                throw Failure(Status::ResourceLimit, _u8L("Surface contour knot budget exceeded"));
            for (std::size_t i = 0; i < unique_count; ++i)
                if (!distinct.insert(contour[i]).second)
                    throw Failure(Status::InvalidMesh, _u8L("A surface contour has an unresolved self-contact"));
            Vector previous_normal(0, 0, 0), first_normal(0, 0, 0);
            for (std::size_t i = 1; i < contour.size(); ++i) {
                check();
                const K::Segment_3 segment(contour[i - 1], contour[i]);
                if (segment.is_degenerate())
                    throw Failure(Status::InvalidMesh, _u8L("A surface contour contains a collapsed segment"));
                std::vector<Mesh::Face_index> candidates;
                faces.all_intersected_primitives(segment, std::back_inserter(candidates));
                if (candidates.size() > options.max_carrier_candidates - std::min(out.carrier_candidates, options.max_carrier_candidates))
                    throw Failure(Status::ResourceLimit, _u8L("Surface contour carrier-query budget exceeded"));
                out.carrier_candidates += candidates.size();
                int carrier = -1;
                for (auto face : candidates) {
                    check();
                    const int source = source_ids.at(face);
                    const auto& f    = input.surface.faces[source];
                    const auto& a    = input.surface.vertices[f[0]];
                    const auto& b    = input.surface.vertices[f[1]];
                    const auto& c    = input.surface.vertices[f[2]];
                    const K::Triangle_3 triangle(Point(a.x(), a.y(), a.z()), Point(b.x(), b.y(), b.z()), Point(c.x(), c.y(), c.z()));
                    if (triangle.has_on(segment.source()) && triangle.has_on(segment.target())) {
                        if (carrier < 0 || source < carrier)
                            carrier = source;
                        coplanar = coplanar || (plane.has_on(triangle[0]) && plane.has_on(triangle[1]) && plane.has_on(triangle[2]));
                    }
                }
                if (carrier < 0)
                    throw Failure(Status::Failure,
                                  _u8L("A contour segment has no exact single-triangle carrier; no nearest-surface fallback is used"));
                const auto first = state_on_face(segment.source(), carrier), last = state_on_face(segment.target(), carrier);
                if (run.knots.empty()) {
                    const auto converted = convert(first);
                    if (++knots > options.max_knots)
                        throw Failure(Status::ResourceLimit, _u8L("Surface contour knot budget exceeded"));
                    run.knots.push_back(converted.knot);
                    first_normal = first.normal;
                } else if (first.normal != previous_normal)
                    throw Failure(Status::InvalidInput, _u8L("The normal field is discontinuous across a shared triangle boundary"));
                const bool closing_segment = run.closed && i + 1 == contour.size();
                if (closing_segment && last.normal != first_normal)
                    throw Failure(Status::InvalidInput, _u8L("The normal field is discontinuous at the closed contour seam"));
                std::function<void(const State&, const State&, unsigned, bool)> refine;
                refine = [&](const State& a, const State& b, unsigned level, bool closing_endpoint) {
                    check();
                    const auto ca = convert(a), cb = convert(b);
                    const double shift  = std::max(up_add(ca.position_error, up_multiply(options.normal_offset_distance, ca.normal_error)),
                                                   up_add(cb.position_error, up_multiply(options.normal_offset_distance, cb.normal_error)));
                    const double error  = up_add(interpolation_error(a, b, options.normal_offset_distance), shift);
                    const double length = root_interval(CGAL::squared_distance(a.point, b.point)).upper;
                    if (length > options.max_segment_length || error > options.interpolation_tolerance) {
                        if (level >= 40)
                            throw Failure(Status::ResourceLimit,
                                          _u8L(
                                              "Surface normal interpolation cannot reach the requested bound within its refinement limit"));
                        const auto middle = midpoint(a, b);
                        refine(a, middle, level + 1, false);
                        refine(middle, b, level + 1, closing_endpoint);
                        return;
                    }
                    if ((ca.knot.position.array() == cb.knot.position.array()).all())
                        throw Failure(Status::Failure, _u8L("Double conversion collapses a nonzero surface contour segment"));
                    if (!closing_endpoint) {
                        if (++knots > options.max_knots)
                            throw Failure(Status::ResourceLimit, _u8L("Adaptive surface contour knot budget exceeded"));
                        run.knots.push_back(cb.knot);
                    }
                    run.carriers.push_back({carrier, to_double(a.barycentric), to_double(b.barycentric)});
                    out.position_rounding_bound    = std::max({out.position_rounding_bound, ca.position_error, cb.position_error});
                    out.normal_rounding_bound      = std::max({out.normal_rounding_bound, ca.normal_error, cb.normal_error});
                    out.offset_interpolation_bound = std::max(out.offset_interpolation_bound, error);
                };
                refine(first, last, 0, closing_segment);
                previous_normal = last.normal;
            }
            if (run.carriers.size() != run.knots.size() - (run.closed ? 0 : 1))
                throw Failure(Status::Failure, _u8L("Surface contour carriers do not match their motion segments"));
            out.runs.push_back(std::move(run));
        }
    }
    if (coplanar)
        out.warnings.push_back(_u8L("Coplanar surface faces contribute boundary curves only; these curves do not certify area coverage"));
    if (out.isolated_contacts)
        out.warnings.push_back(_u8L("Isolated plane contacts have no extrusion length and are not planned"));
    check();
    out.status                = Status::Complete;
    out.interpolation_bounded = !out.runs.empty();
    if (out.runs.empty()) {
        out.reason                  = _u8L("No nondegenerate contour intersects the selected surface");
        out.position_rounding_bound = out.normal_rounding_bound = out.offset_interpolation_bound = INFINITY;
    }
    return out;
}
} // namespace Slic3r::FormorixSurfacePaths::detail
