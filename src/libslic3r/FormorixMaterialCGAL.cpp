#include "FormorixMaterial.hpp"
#include "FormorixMaterialInternal.hpp"
#include "FormorixGeometry.hpp"
#include "Point.hpp"
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits_3.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>
#include <CGAL/Bbox_3.h>
#include <CGAL/Kernel/global_functions_3.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/boost/graph/copy_face_graph.h>
#include <CGAL/Polygon_mesh_processing/corefinement.h>
#include <CGAL/Polygon_mesh_processing/measure.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/Polygon_mesh_processing/self_intersections.h>
#include <CGAL/Polygon_mesh_processing/repair.h>
#include <CGAL/number_utils.h>
#include <CGAL/Lazy.h>
#include <CGAL/Lazy_exact_nt.h>
#include <CGAL/Polygon_2.h>
#include <CGAL/Polygon_set_2.h>
#include <CGAL/squared_distance_3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <iterator>
#include <map>
#include <optional>
#include <queue>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace Slic3r::FormorixMaterial::detail {
namespace {
using K        = CGAL::Exact_predicates_exact_constructions_kernel;
using Exact    = CGAL::Surface_mesh<K::Point_3>;
using Point    = K::Point_3;
using Triangle = K::Triangle_3;
namespace PMP  = CGAL::Polygon_mesh_processing;
struct DistanceLimit
{};
using Rational = K::FT::Exact_type;

void add_exact(K::FT& total, const K::FT& value)
{
    Rational sum = CGAL::exact(total) + CGAL::exact(value);
    total        = K::FT(std::move(sum));
}
K::FT volume(const Exact& mesh)
{
    // Lazy += over a large mesh builds a linear DAG whose evaluation/destruction
    // can overflow the stack. Accumulate exact rationals without a lazy chain.
    Rational sum(0);
    const Point origin(0, 0, 0);
    for (auto f : mesh.faces()) {
        const auto h     = mesh.halfedge(f);
        const auto value = CGAL::volume(origin, mesh.point(mesh.source(h)), mesh.point(mesh.target(h)),
                                        mesh.point(mesh.target(mesh.next(h))));
        sum += CGAL::exact(value);
    }
    return K::FT(std::move(sum));
}

struct Control
{
    Result& out;
    const Options& options;
    const std::function<bool()>& canceled;
    std::size_t distance_leaves  = 0;
    std::size_t distance_ceiling = options.max_distance_queries;
    void check() const
    {
        if (canceled && canceled())
            throw Failure(Status::Canceled, "Material volume audit canceled");
    }
    void distance_query()
    {
        if (out.distance_queries >= distance_ceiling)
            throw DistanceLimit{};
        ++out.distance_queries;
    }
    void faces(const Exact& mesh) const
    {
        check();
        out.peak_boolean_faces = std::max(out.peak_boolean_faces, std::size_t(mesh.number_of_faces()));
        if (mesh.number_of_faces() > options.max_boolean_faces || mesh.number_of_vertices() > options.max_boolean_faces)
            throw Failure(Status::ResourceLimit, "Material Boolean face budget exceeded");
    }
};
struct Visitor : PMP::Corefinement::Default_visitor<Exact>
{
    Control* control;
    explicit Visitor(Control& value) : control(&value) {}
    void progress_filtering_intersections(double) const { control->check(); }
    void triangulating_faces_step() const { control->check(); }
    void intersection_of_coplanar_faces_step() const { control->check(); }
    void edge_face_intersections_step() const { control->check(); }
    void after_subface_created(Exact::Face_index, const Exact& mesh) { control->faces(mesh); }
    void after_face_copy(Exact::Face_index, const Exact&, Exact::Face_index, const Exact& mesh) { control->faces(mesh); }
    void new_vertex_added(std::size_t, Exact::Vertex_index, const Exact& mesh) { control->faces(mesh); }
};
bool valid(const Exact& mesh)
{
    if (mesh.is_empty() || !mesh.is_valid(false) || !CGAL::is_valid_polygon_mesh(mesh) || !CGAL::is_triangle_mesh(mesh) ||
        !CGAL::is_closed(mesh))
        return false;
    // Volume orientation routines require an incident halfedge at every vertex.
    // A failed or partially built Boolean may contain isolated vertices.
    for (auto v : mesh.vertices())
        if (mesh.halfedge(v) == Exact::null_halfedge())
            return false;
    return !PMP::does_self_intersect(mesh) && PMP::does_bound_a_volume(mesh) && volume(mesh) > 0;
}
double number(const K::FT& value)
{
    const double result = CGAL::to_double(value);
    if (!std::isfinite(result))
        throw Failure(Status::InvalidInput, "Nonfinite material volume");
    return result;
}
Exact exact(const FormorixGeometry::Mesh& mesh, Control& control, bool cell = false)
{
    control.check();
    if (mesh.vertices.size() > control.options.max_boolean_faces || mesh.faces.size() > control.options.max_boolean_faces)
        throw Failure(Status::ResourceLimit, "Material Boolean input exceeds face budget");
    Exact result;
    std::vector<Exact::Vertex_index> indices;
    std::map<std::tuple<double, double, double>, Exact::Vertex_index> unique;
    for (const auto& p : mesh.vertices) {
        if (!p.allFinite() || p.cwiseAbs().maxCoeff() > 1e7)
            throw Failure(Status::InvalidInput, "Invalid material mesh coordinate");
        const auto key   = std::make_tuple(p.x(), p.y(), p.z());
        const auto found = unique.find(key);
        const auto index = cell && found != unique.end() ? found->second : result.add_vertex(Point(p.x(), p.y(), p.z()));
        indices.push_back(index);
        unique.emplace(key, index);
    }
    for (const auto& f : mesh.faces) {
        for (int i : f)
            if (i < 0 || std::size_t(i) >= mesh.vertices.size())
                throw Failure(Status::InvalidInput, "Invalid material mesh index");
        const auto a = indices[f[0]], b = indices[f[1]], c = indices[f[2]];
        const bool degenerate = a == b || b == c || c == a || CGAL::collinear(result.point(a), result.point(b), result.point(c));
        if (cell && degenerate)
            continue; // exact zero-area face at a collapsed inner miter rail; no geometric area is removed
        if (degenerate || result.add_face(a, b, c) == Exact::null_face())
            throw Failure(Status::InvalidSweep, "Degenerate or nonmanifold material mesh");
    }
    if (cell)
        PMP::remove_isolated_vertices(result);
    if (!valid(result))
        throw Failure(Status::InvalidSweep, "Material mesh is folded, self-intersecting or does not bound a volume");
    return result;
}
Point rounded(const Point& p, double& bound)
{
    std::array<double, 3> q;
    for (int i = 0; i < 3; ++i)
        q[i] = CGAL::to_double(p[i]);
    const Point converted(q[0], q[1], q[2]);
    const auto squared  = CGAL::squared_distance(p, converted);
    const auto interval = CGAL::to_interval(squared);
    if (squared != 0)
        bound = std::max(bound, std::nextafter(std::sqrt(std::max(0., interval.second)), INFINITY));
    return converted;
}
Exact snap(const Exact& source, double& bound, Control& control)
{
    Exact result = source;
    for (auto v : result.vertices())
        result.point(v) = rounded(source.point(v), bound);
    control.faces(result);
    if (!valid(result))
        throw Failure(Status::InvalidSweep, "Double material output collapses topology");
    return result;
}
FormorixGeometry::Mesh materialize(const Exact& source, Control& control)
{
    if (source.is_empty())
        return {};
    // No topological repair: every output triangle has the same correspondence,
    // hence the maximum vertex displacement bounds its entire triangle.
    const Exact rounded_mesh = snap(source, control.out.exact_to_output_bound, control);
    FormorixGeometry::Mesh result;
    std::map<Exact::Vertex_index, int> indices;
    for (auto v : rounded_mesh.vertices()) {
        indices[v]    = int(result.vertices.size());
        const auto& p = rounded_mesh.point(v);
        result.vertices.emplace_back(CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
    }
    for (auto f : rounded_mesh.faces()) {
        auto h = rounded_mesh.halfedge(f);
        result.faces.emplace_back(indices.at(rounded_mesh.source(h)), indices.at(rounded_mesh.target(h)),
                                  indices.at(rounded_mesh.target(rounded_mesh.next(h))));
    }
    return result;
}
CGAL::Bbox_3 box(const Exact& mesh)
{
    CGAL::Bbox_3 result;
    for (auto v : mesh.vertices())
        result += mesh.point(v).bbox();
    return result;
}
bool separate(const Exact& a, const Exact& b)
{
    const auto x = box(a), y = box(b);
    return x.xmax() < y.xmin() || y.xmax() < x.xmin() || x.ymax() < y.ymin() || y.ymax() < x.ymin() || x.zmax() < y.zmin() ||
           y.zmax() < x.zmin();
}
Exact boolean(Exact a, Exact b, int op, Control& control, const std::string& phase = "material audit")
{
    control.check();
    if (a.is_empty())
        return op == 0 ? b : Exact{};
    if (b.is_empty())
        return op == 1 ? Exact{} : a;
    if (separate(a, b)) {
        if (op == 1)
            return {};
        if (op == 2)
            return a;
        CGAL::copy_face_graph(b, a);
        control.faces(a);
        return a;
    }
    if (++control.out.boolean_operations > control.options.max_boolean_operations)
        throw Failure(Status::ResourceLimit, "Material Boolean operation budget exceeded");
    control.faces(a);
    control.faces(b);
    const auto input_a = a.number_of_faces(), input_b = b.number_of_faces();
    Exact result;
    std::array<std::optional<Exact*>, 4> outputs{};
    outputs[op] = &result;
    const auto ok =
        PMP::corefine_and_compute_boolean_operations(a, b, outputs,
                                                     CGAL::parameters::visitor(Visitor(control)).throw_on_self_intersection(true));
    const auto failure = [&] {
        return Failure(Status::BooleanFailure, phase + ": Boolean operation " + std::to_string(op) + " failed (work " +
                                                   std::to_string(control.out.boolean_operations) + ", input faces " +
                                                   std::to_string(input_a) + "/" + std::to_string(input_b) +
                                                   "): nonmanifold or invalid result");
    };
    // CGAL does not promise a usable output when this flag is false. Never
    // evaluate its lazy coordinates or traverse a partial result in that case.
    if (!ok[op])
        throw failure();
    control.faces(result);
    if (!result.is_empty() && (!result.is_valid(false) || !CGAL::is_valid_polygon_mesh(result)))
        throw failure();
    for (auto v : result.vertices()) {
        control.check();
        const auto p    = result.point(v);
        result.point(v) = Point(K::FT(CGAL::exact(p.x())), K::FT(CGAL::exact(p.y())), K::FT(CGAL::exact(p.z())));
    }
    if (!result.is_empty() && !valid(result))
        throw failure();
    return result;
}
Exact unite(std::vector<Exact> meshes, Control& control, const std::string& phase, bool preserve_order = false)
{
    if (meshes.empty())
        return {};
    // A run keeps adjacent cells together: sorting its cells spatially can
    // create a nonmanifold point contact before the connecting turn is present.
    // Completed runs use spatial order. Reduction remains balanced in both cases.
    if (!preserve_order)
        std::stable_sort(meshes.begin(), meshes.end(), [](const Exact& a, const Exact& b) { return box(a).xmin() < box(b).xmin(); });
    while (meshes.size() > 1) {
        std::vector<Exact> next;
        next.reserve((meshes.size() + 1) / 2);
        for (std::size_t i = 0; i < meshes.size(); i += 2) {
            control.check();
            next.push_back(i + 1 < meshes.size() ? boolean(std::move(meshes[i]), std::move(meshes[i + 1]), 0, control, phase) :
                                                   std::move(meshes[i]));
        }
        meshes = std::move(next);
    }
    return std::move(meshes.front());
}
Triangle triangle(const Exact& mesh, Exact::Face_index f)
{
    const auto h = mesh.halfedge(f);
    return Triangle(mesh.point(mesh.source(h)), mesh.point(mesh.target(h)), mesh.point(mesh.target(mesh.next(h))));
}
struct Interval
{
    double low, high;
};
Interval distance_interval(const K::FT& squared)
{
    if (squared == 0)
        return {0., 0.};
    const auto interval = CGAL::to_interval(squared);
    return {std::max(0., std::nextafter(std::sqrt(std::max(0., interval.first)), -INFINITY)),
            std::nextafter(std::sqrt(std::max(0., interval.second)), INFINITY)};
}
struct Leaf
{
    std::array<Point, 3> points;
    double lower = 0., upper = 0., center_distance = 0., area = 0.;
    bool operator<(const Leaf& other) const { return upper < other.upper; }
};
Distance directed_distance(const Exact& source, const Exact& target, Control& control)
{
    Distance out;
    if (source.is_empty() || target.is_empty()) {
        if (source.is_empty() && target.is_empty()) {
            out.maximum = out.mean = out.rms = 0.;
            out.upper_bound                  = 0.;
            out.bounded                      = true;
        } else
            out.maximum = out.mean = out.rms = INFINITY;
        return out;
    }
    out.maximum = out.mean = out.rms = 0.;
    try {
        using Primitive = CGAL::AABB_face_graph_triangle_primitive<Exact>;
        using Traits    = CGAL::AABB_traits_3<K, Primitive>;
        CGAL::AABB_tree<Traits> tree(target.faces().begin(), target.faces().end(), target);
        tree.accelerate_distance_queries();
        auto evaluate = [&](const std::array<Point, 3>& points) {
            control.check();
            Leaf leaf;
            leaf.points        = points;
            const Point center = CGAL::centroid(points[0], points[1], points[2]);
            std::array<Point, 4> samples{{points[0], points[1], points[2], center}};
            std::array<Exact::Face_index, 4> candidates;
            Interval middle{};
            for (std::size_t i = 0; i < samples.size(); ++i) {
                control.distance_query();
                const auto nearest  = tree.closest_point_and_primitive(samples[i]);
                candidates[i]       = nearest.second;
                const auto interval = distance_interval(CGAL::squared_distance(samples[i], nearest.first));
                leaf.lower          = std::max(leaf.lower, interval.low);
                out.maximum         = std::max(out.maximum, .5 * (interval.low + interval.high));
                ++out.samples;
                if (i == 3)
                    middle = interval;
            }
            leaf.center_distance = .5 * (middle.low + middle.high);
            double radius        = 0.;
            for (const auto& point : points)
                radius = std::max(radius, distance_interval(CGAL::squared_distance(center, point)).high);
            leaf.upper = std::nextafter(middle.high + radius, INFINITY);
            // Distance to any fixed convex triangle is convex. Its maximum over
            // the complete source triangle is bounded by its three vertex values.
            for (auto f : candidates) {
                const Triangle candidate = triangle(target, f);
                double bound             = 0.;
                for (const auto& point : points) {
                    control.distance_query();
                    bound = std::max(bound, distance_interval(CGAL::squared_distance(point, candidate)).high);
                }
                leaf.upper = std::min(leaf.upper, bound);
            }
            leaf.area = std::sqrt(std::max(0., number(Triangle(points[0], points[1], points[2]).squared_area())));
            return leaf;
        };
        std::priority_queue<Leaf> leaves;
        for (auto f : source.faces()) {
            if (++control.distance_leaves > control.options.max_distance_leaves) {
                out.mean = out.rms = INFINITY;
                if (out.samples == 0)
                    out.maximum = INFINITY;
                return out;
            }
            const auto t    = triangle(source, f);
            auto leaf       = evaluate({{t[0], t[1], t[2]}});
            out.lower_bound = std::max(out.lower_bound, leaf.lower);
            leaves.push(std::move(leaf));
        }
        bool exhausted = false;
        while (!leaves.empty() && leaves.top().upper - out.lower_bound > control.options.distance_slack) {
            control.check();
            if (control.distance_leaves + 3 > control.options.max_distance_leaves) {
                exhausted = true;
                break;
            }
            control.distance_leaves += 3;
            const auto p = leaves.top().points;
            leaves.pop();
            const auto ab = CGAL::midpoint(p[0], p[1]), bc = CGAL::midpoint(p[1], p[2]), ca = CGAL::midpoint(p[2], p[0]);
            for (const auto& points :
                 std::array<std::array<Point, 3>, 4>{{{{p[0], ab, ca}}, {{ab, p[1], bc}}, {{ca, bc, p[2]}}, {{ab, bc, ca}}}}) {
                auto leaf       = evaluate(points);
                out.lower_bound = std::max(out.lower_bound, leaf.lower);
                leaves.push(std::move(leaf));
            }
        }
        if (!exhausted) {
            out.upper_bound = leaves.empty() ? 0. : leaves.top().upper;
            out.bounded     = true;
        }
        double area = 0., sum = 0., squares = 0.;
        while (!leaves.empty()) {
            const auto& leaf = leaves.top();
            area += leaf.area;
            sum += leaf.area * leaf.center_distance;
            squares += leaf.area * leaf.center_distance * leaf.center_distance;
            leaves.pop();
        }
        if (area > 0.) {
            out.mean = sum / area;
            out.rms  = std::sqrt(squares / area);
        }
        if (exhausted)
            out.mean = out.rms = INFINITY;
    } catch (const DistanceLimit&) {
        out.bounded     = false;
        out.upper_bound = out.mean = out.rms = INFINITY;
        if (out.samples == 0)
            out.maximum = INFINITY;
    }
    return out;
}
bool identical_coplanar(const Exact& a, const Exact& b, Control& control)
{
    const std::size_t triangles = a.number_of_faces() + b.number_of_faces();
    if (a.is_empty() || b.is_empty() || triangles > 128)
        return false;
    const std::size_t edges = triangles * 3;
    // An arrangement can grow quadratically despite a small input. Bound all
    // pairwise segment intersections plus endpoints before entering Polygon_set.
    if (edges * (edges - 1) / 2 + 2 * edges > control.options.max_boolean_faces)
        return false;
    const auto first = triangle(a, *a.faces().begin());
    const K::Plane_3 plane(first[0], first[1], first[2]);
    for (const Exact* mesh : {&a, &b})
        for (auto v : mesh->vertices()) {
            control.check();
            if (!plane.has_on(mesh->point(v)))
                return false;
        }
    if (control.out.boolean_operations + 4 > control.options.max_boolean_operations)
        return false;
    using Polygon  = CGAL::Polygon_2<K>;
    auto footprint = [&](const Exact& mesh) {
        std::vector<Polygon> polygons;
        polygons.reserve(mesh.number_of_faces());
        for (auto f : mesh.faces()) {
            control.check();
            const auto t = triangle(mesh, f);
            Polygon polygon;
            for (int i = 0; i < 3; ++i)
                polygon.push_back(plane.to_2d(t[i]));
            if (polygon.is_clockwise_oriented())
                polygon.reverse_orientation();
            polygons.push_back(std::move(polygon));
        }
        CGAL::Polygon_set_2<K> result;
        ++control.out.boolean_operations;
        result.join(polygons.begin(), polygons.end());
        control.check();
        return result;
    };
    const auto fa = footprint(a), fb = footprint(b);
    auto missing = fa, outside = fb;
    ++control.out.boolean_operations;
    missing.difference(fb);
    control.check();
    ++control.out.boolean_operations;
    outside.difference(fa);
    control.check();
    // Exact set identity proves every triangle point is covered, regardless of
    // triangulation. A missing strip or hole never passes this shortcut.
    return missing.is_empty() && outside.is_empty();
}
Distance symmetric_distance(const Exact& a, const Exact& b, bool identical, Control& control)
{
    if (identical || identical_coplanar(a, b, control)) {
        Distance out;
        out.maximum = out.mean = out.rms = 0.;
        out.upper_bound                  = 0.;
        out.bounded                      = true;
        return out;
    }
    const auto ceiling   = control.distance_ceiling;
    const auto available = ceiling - std::min(control.out.distance_queries, ceiling);
    // Give both directions an opportunity to see cavities before refinement of
    // a nearly coincident exterior consumes the entire shared query allowance.
    control.distance_ceiling = control.out.distance_queries + available / 2;
    const auto first         = directed_distance(a, b, control);
    control.distance_ceiling = ceiling;
    const auto second        = directed_distance(b, a, control);
    Distance out;
    out.maximum     = std::max(first.maximum, second.maximum);
    out.mean        = .5 * (first.mean + second.mean);
    out.rms         = std::sqrt(.5 * (first.rms * first.rms + second.rms * second.rms));
    out.samples     = first.samples + second.samples;
    out.lower_bound = std::max(first.lower_bound, second.lower_bound);
    out.upper_bound = std::max(first.upper_bound, second.upper_bound);
    out.bounded     = first.bounded && second.bounded;
    return out;
}
Exact sheet(const FormorixGeometry::Mesh& source, Control& control)
{
    if (source.faces.size() > control.options.max_boolean_faces || source.vertices.size() > control.options.max_boolean_faces)
        throw Failure(Status::ResourceLimit, "Material outer sheet exceeds face budget");
    Exact result;
    // Sheets are a triangle collection. Keeping distinct vertices is intentional:
    // the nearest-surface calculation does not require manifold chart seams.
    for (const auto& face : source.faces) {
        std::array<Point, 3> p;
        for (int i = 0; i < 3; ++i) {
            if (face[i] < 0 || std::size_t(face[i]) >= source.vertices.size() || !source.vertices[face[i]].allFinite())
                throw Failure(Status::InvalidInput, "Invalid nominal outer sheet");
            const auto& v = source.vertices[face[i]];
            p[i]          = Point(v.x(), v.y(), v.z());
        }
        if (!CGAL::collinear(p[0], p[1], p[2]))
            result.add_face(result.add_vertex(p[0]), result.add_vertex(p[1]), result.add_vertex(p[2]));
    }
    control.faces(result);
    return result;
}
void append_roof(FormorixGeometry::Mesh& output, const FormorixGeometry::Mesh& cell)
{
    const int base = int(output.vertices.size());
    for (int i : {2, 3, 7, 6})
        output.vertices.push_back(cell.vertices[i]);
    output.faces.emplace_back(base, base + 1, base + 2);
    output.faces.emplace_back(base, base + 2, base + 3);
}
} // namespace

std::vector<Station> simplify_stations(const std::vector<Station>& input)
{
    std::vector<Station> result;
    auto same_section = [](const Station& a, const Station& b) {
        return a.width == b.width && a.height == b.height && (a.axis.array() == b.axis.array()).all() &&
               bool(a.height_endpoints) == bool(b.height_endpoints);
    };
    auto point = [](const Vec3d& p) { return Point(p.x(), p.y(), p.z()); };
    for (const auto& s : input) {
        if (result.size() >= 2 && same_section(result[result.size() - 2], result.back()) && same_section(result.back(), s)) {
            const auto a = point(result[result.size() - 2].center), b = point(result.back().center), c = point(s.center);
            bool collinear = a != b && b != c && CGAL::collinear(a, b, c) && CGAL::collinear_are_ordered_along_line(a, b, c);
            if (collinear && s.height_endpoints)
                for (int k = 0; k < 2; ++k) {
                    const auto x = point((*result[result.size() - 2].height_endpoints)[k]);
                    const auto y = point((*result.back().height_endpoints)[k]);
                    const auto z = point((*s.height_endpoints)[k]);
                    collinear    = collinear && CGAL::collinear(x, y, z) && CGAL::collinear_are_ordered_along_line(x, y, z);
                }
            if (collinear) {
                result.back() = s;
                continue;
            }
        }
        result.push_back(s);
    }
    return result;
}

void audit_exact(Result& out,
                 const std::vector<Target>& targets,
                 const FormorixGeometry::Mesh& core,
                 const std::vector<SweepRun>& runs,
                 const std::vector<Cell>& cells,
                 const Options& options,
                 const std::function<bool()>& canceled)
{
    Control control{out, options, canceled};
    std::map<int, Exact> target_meshes;
    std::map<int, std::vector<Exact>> region_cells;
    std::vector<std::vector<Exact>> run_cells(runs.size());
    std::map<int, K::FT> cell_volumes, supply_volumes;
    std::map<std::string, std::vector<Exact>> material_regions;
    std::map<int, FormorixGeometry::Mesh> outer_sheets;
    std::vector<Exact> target_parts;
    for (const auto& target : targets) {
        cell_volumes.emplace(target.region, K::FT(0));
        supply_volumes.emplace(target.region, K::FT(0));
        Exact mesh = exact(target.mesh, control);
        for (const auto& previous : target_parts)
            if (volume(boolean(mesh, previous, 1, control, "target overlap")) != 0)
                throw Failure(Status::InvalidInput, "Material target regions have overlapping interiors");
        target_parts.push_back(mesh);
        target_meshes.emplace(target.region, std::move(mesh));
    }
    for (const auto& run : runs)
        for (double supply : run.supplied_volumes)
            add_exact(supply_volumes.at(run.region), K::FT(supply));
    for (std::size_t index = 0; index < cells.size(); ++index) {
        const auto& cell = cells[index];
        control.check();
        const auto region = runs[cell.run].region;
        Exact mesh;
        try {
            mesh = exact(cell.mesh, control, true);
        } catch (const Failure& e) {
            throw Failure(e.status, "Material run " + std::to_string(cell.run) + ", cell " + std::to_string(index) + ": " + e.what());
        }
        const auto v = volume(mesh);
        add_exact(cell_volumes.at(region), v);
        run_cells[cell.run].push_back(std::move(mesh));
        if (runs[cell.run].outer_layer) {
            append_roof(outer_sheets[region], cell.mesh);
            append_roof(out.nominal_outer_sheet, cell.mesh);
        }
    }
    for (std::size_t r = 0; r < runs.size(); ++r)
        region_cells[runs[r].region].push_back(unite(std::move(run_cells[r]), control, "run " + std::to_string(r), true));
    const Exact all_targets = unite(std::move(target_parts), control, "target union");
    const Exact core_mesh   = core.faces.empty() && core.vertices.empty() ? Exact{} : exact(core, control);
    std::vector<Exact> deposited_regions;
    K::FT all_cells(0), all_supply(0);
    for (const auto& target : targets) {
        Exact deposited = unite(std::move(region_cells[target.region]), control, "region " + std::to_string(target.region));
        deposited_regions.push_back(deposited);
        material_regions[target.material].push_back(deposited);
        const auto& wanted       = target_meshes.at(target.region);
        const Exact missing      = boolean(wanted, deposited, 2, control, "regional missing");
        const Exact outside      = boolean(deposited, wanted, 2, control, "regional outside");
        const Exact core_overlap = boolean(deposited, core_mesh, 1, control, "regional core overlap");
        const auto v             = volume(deposited);
        if (cell_volumes.at(target.region) < v)
            throw Failure(Status::BooleanFailure, "Material union exceeds the sum of its cells");
        Region region;
        region.region         = target.region;
        region.material       = target.material;
        region.target_volume  = number(volume(wanted));
        region.missing_volume = number(volume(missing));
        region.covered_volume = number(volume(wanted) - volume(missing));
        region.coverage = missing.is_empty() ? Coverage::Full : volume(wanted) == volume(missing) ? Coverage::Empty : Coverage::Partial;
        region.deposited_volume            = number(v);
        region.outside_volume              = number(volume(outside));
        region.supplied_volume             = number(supply_volumes.at(target.region));
        region.cell_volume                 = number(cell_volumes.at(target.region));
        region.duplicate_volume            = number(cell_volumes.at(target.region) - v);
        region.core_overlap_volume         = number(volume(core_overlap));
        region.supply_geometry_residual    = number(supply_volumes.at(target.region) - cell_volumes.at(target.region));
        region.input_rounding_volume_bound = 0.;
        region.boundary_distance           = symmetric_distance(wanted, deposited, missing.is_empty() && outside.is_empty(), control);
        if (!target.outer_surface.faces.empty())
            region.nominal_outer_distance = symmetric_distance(sheet(target.outer_surface, control),
                                                               sheet(outer_sheets[target.region], control), false, control);
        region.missing = materialize(missing, control);
        region.outside = materialize(outside, control);
        out.regions.push_back(std::move(region));
        add_exact(all_cells, cell_volumes.at(target.region));
        add_exact(all_supply, supply_volumes.at(target.region));
    }
    std::map<std::string, Exact> materials;
    for (auto& entry : material_regions)
        materials.emplace(entry.first, unite(std::move(entry.second), control, "material " + entry.first));
    for (auto first = materials.begin(); first != materials.end(); ++first)
        for (auto second = std::next(first); second != materials.end(); ++second) {
            const Exact overlap = boolean(first->second, second->second, 1, control, "cross-material overlap");
            if (!overlap.is_empty())
                out.material_overlaps.push_back({first->first, second->first, number(volume(overlap)), materialize(overlap, control)});
        }
    const Exact deposited = unite(std::move(deposited_regions), control, "complete deposited union");
    const Exact missing   = boolean(all_targets, deposited, 2, control, "complete missing");
    const Exact outside   = boolean(deposited, all_targets, 2, control, "complete outside");
    const Exact overlap   = boolean(deposited, core_mesh, 1, control, "complete core overlap");
    if (all_cells < volume(deposited))
        throw Failure(Status::BooleanFailure, "Combined material union exceeds supplied cell geometry");
    out.supplied_volume             = number(all_supply);
    out.cell_volume                 = number(all_cells);
    out.union_volume                = number(volume(deposited));
    out.duplicate_volume            = number(all_cells - volume(deposited));
    out.supply_geometry_residual    = number(all_supply - all_cells);
    out.input_rounding_volume_bound = 0.;
    out.missing_volume              = number(volume(missing));
    out.outside_volume              = number(volume(outside));
    out.core_overlap_volume         = number(volume(overlap));
    out.boundary_distance           = targets.size() == 1 ?
                                          out.regions.front().boundary_distance :
                                          symmetric_distance(all_targets, deposited, missing.is_empty() && outside.is_empty(), control);
    FormorixGeometry::Mesh all_outer;
    bool all_outer_known = true;
    for (const auto& target : targets) {
        if (target.outer_surface.faces.empty()) {
            all_outer_known = false;
            break;
        }
        const int base = int(all_outer.vertices.size());
        all_outer.vertices.insert(all_outer.vertices.end(), target.outer_surface.vertices.begin(), target.outer_surface.vertices.end());
        for (const auto& face : target.outer_surface.faces)
            all_outer.faces.emplace_back(base + face[0], base + face[1], base + face[2]);
    }
    if (all_outer_known)
        out.nominal_outer_distance = targets.size() == 1 ? out.regions.front().nominal_outer_distance :
                                                           symmetric_distance(sheet(all_outer, control),
                                                                              sheet(out.nominal_outer_sheet, control), false, control);
    out.deposited    = materialize(deposited, control);
    out.missing      = materialize(missing, control);
    out.outside      = materialize(outside, control);
    out.core_overlap = materialize(overlap, control);
    if (!deposited.is_empty())
        out.output_volume_residual = number(volume(exact(out.deposited, control)) - volume(deposited));
    out.rounding_bound = out.construction_rounding_bound + out.exact_to_output_bound;
    // No pre-union snapping is permitted: it could erase an interior gap and
    // destroy a boundary-distance proof despite a tiny vertex displacement.
    // Exact raw cells are authoritative; this additional term covers display conversion.
    if (out.boundary_distance.bounded)
        out.boundary_distance.upper_bound = std::nextafter(out.boundary_distance.upper_bound + out.rounding_bound, INFINITY);
    for (auto& region : out.regions)
        if (region.boundary_distance.bounded)
            region.boundary_distance.upper_bound = std::nextafter(region.boundary_distance.upper_bound + out.rounding_bound, INFINITY);
}
} // namespace Slic3r::FormorixMaterial::detail
