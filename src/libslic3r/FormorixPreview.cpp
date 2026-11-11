#include "FormorixPreview.hpp"
#include "FormorixProcess.hpp"
#include "AABBTreeIndirect.hpp"
#include "FormorixSurfaces.hpp"
#include "Model.hpp"
#include "Point.hpp"
#include "TriangleSelector.hpp"
#include "ClipperUtils.hpp"
#include "Polygon.hpp"
#include "libslic3r.h"
#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <exception>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace Slic3r::FormorixPreview {
namespace {
constexpr double eps = 1e-6;
constexpr double pi  = 3.14159265358979323846;
bool finite(const Vec3d& p) { return p.allFinite(); }
// Keep this traversal local to the preview: zero direction components on flat
// triangle boxes must not form 0*infinity/NaN. The ordinary Orca raycaster is unchanged.
class WallRaycaster
{
    const indexed_triangle_set& mesh;
    AABBTreeIndirect::Tree3f tree;
    void trace(std::size_t index, const Vec3d& origin, const Vec3d& dir, double& distance, bool& inside) const
    {
        const auto& node = tree.node(index);
        double low = 0., high = distance;
        for (int a = 0; a < 3; ++a) {
            if (std::abs(dir[a]) < 1e-15) {
                if (origin[a] < node.bbox.min()[a] - eps || origin[a] > node.bbox.max()[a] + eps)
                    return;
            } else {
                double x = (node.bbox.min()[a] - eps - origin[a]) / dir[a], y = (node.bbox.max()[a] + eps - origin[a]) / dir[a];
                if (x > y)
                    std::swap(x, y);
                low  = std::max(low, x);
                high = std::min(high, y);
                if (low > high)
                    return;
            }
        }
        if (node.is_inner()) {
            trace(index * 2 + 1, origin, dir, distance, inside);
            trace(index * 2 + 2, origin, dir, distance, inside);
            return;
        }
        const auto& t = mesh.indices[node.idx];
        const Vec3d a = mesh.vertices[t[0]].cast<double>(), b = mesh.vertices[t[1]].cast<double>() - a,
                    c = mesh.vertices[t[2]].cast<double>() - a;
        const Vec3d p = dir.cross(c), delta = origin - a, q = delta.cross(b);
        const double determinant = b.dot(p);
        if (std::abs(determinant) < 1e-12)
            return;
        const double u = delta.dot(p) / determinant, v = dir.dot(q) / determinant, d = c.dot(q) / determinant;
        if (u >= -1e-10 && v >= -1e-10 && u + v <= 1. + 1e-10 && d > eps && d < distance) {
            distance = d;
            inside   = b.cross(c).dot(dir) > 0.;
        }
    }

public:
    explicit WallRaycaster(const indexed_triangle_set& source)
        : mesh(source), tree(AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(source.vertices, source.indices))
    {}
    double thickness(const Vec3d& point, const Vec3d& normal) const
    {
        double distance = std::numeric_limits<double>::infinity();
        bool inside     = false;
        trace(0, point - normal * 1e-4, -normal, distance, inside);
        if (!inside || !std::isfinite(distance))
            throw Issue::ThinWall;
        return distance + 1e-4;
    }
};
void append(indexed_triangle_set& out, const indexed_triangle_set& in, const Transform3d& transform)
{
    const int base = int(out.vertices.size());
    for (const auto& v : in.vertices)
        out.vertices.push_back((transform * v.cast<double>()).cast<float>());
    for (auto t : in.indices) {
        if (transform.linear().determinant() < 0.)
            std::swap(t[1], t[2]);
        out.indices.emplace_back(t[0] + base, t[1] + base, t[2] + base);
    }
}
indexed_triangle_set weld(const indexed_triangle_set& in)
{
    indexed_triangle_set out;
    std::map<std::tuple<long long, long long, long long>, int> ids;
    for (const auto& t : in.indices) {
        Vec3i32 face;
        for (int j = 0; j < 3; ++j) {
            if (t[j] < 0 || std::size_t(t[j]) >= in.vertices.size())
                throw Issue::InvalidMesh;
            const Vec3d v = in.vertices[t[j]].cast<double>();
            if (!finite(v) || v.cwiseAbs().maxCoeff() > 1e7)
                throw Issue::InvalidMesh;
            const auto key      = std::make_tuple(std::llround(v.x() / eps), std::llround(v.y() / eps), std::llround(v.z() / eps));
            auto [it, inserted] = ids.emplace(key, int(out.vertices.size()));
            if (inserted)
                out.vertices.push_back(v.cast<float>());
            face[j] = it->second;
        }
        if (face[0] == face[1] || face[1] == face[2] || face[2] == face[0])
            throw Issue::InvalidMesh;
        out.indices.push_back(face);
    }
    return out;
}
double cross(const Vec2d& a, const Vec2d& b) { return a.x() * b.y() - a.y() * b.x(); }
bool intersects(const Vec2d& a, const Vec2d& b, const Vec2d& c, const Vec2d& d)
{
    const double x = cross(b - a, c - a), y = cross(b - a, d - a), z = cross(d - c, a - c), w = cross(d - c, b - c);
    return x * y < -1e-14 && z * w < -1e-14;
}
void simple_boundary(const std::vector<Vec2d>& p)
{
    if (p.size() > 2048)
        throw Issue::TooLarge;
    for (std::size_t i = 0; i < p.size(); ++i)
        for (std::size_t j = i + 2; j < p.size(); ++j) {
            if (i == 0 && j + 1 == p.size())
                continue;
            if (intersects(p[i], p[(i + 1) % p.size()], p[j], p[(j + 1) % p.size()]))
                throw Issue::InvalidOffset;
        }
}
struct Patch
{
    indexed_triangle_set mesh;
    std::vector<Vec3d> normals;
    std::vector<Vec2d> uv;
    std::vector<int> boundary;
    std::vector<Vec2d> outline;
    Vec3d u, v, axis;
    int constant_axis = -1;
    Vec2d lo, hi;
    int cells = 1;
    std::vector<std::vector<int>> grid;
    int cell(const Vec2d& p) const
    {
        const Vec2d q = (p - lo).cwiseQuotient(hi - lo);
        return std::clamp(int(q.y() * cells), 0, cells - 1) * cells + std::clamp(int(q.x() * cells), 0, cells - 1);
    }
    Pose at(const Vec2d& p) const
    {
        for (int i : grid[cell(p)]) {
            const auto& t = mesh.indices[i];
            const Vec2d a = uv[t[0]], b = uv[t[1]], c = uv[t[2]];
            const double area = cross(b - a, c - a);
            const double y = cross(p - a, c - a) / area, z = cross(b - a, p - a) / area, x = 1. - y - z;
            if (std::min({x, y, z}) < -2e-5)
                continue;
            // On an axis-aligned plane, retain the analytically constructed
            // raster coordinates. Barycentric reconstruction can introduce a
            // tiny zigzag whose short segments fold a finite-width miter.
            if (constant_axis >= 0) {
                Vec3d position = u * p.x() + v * p.y();
                position[constant_axis] = mesh.vertices.front()[constant_axis];
                return {position, axis};
            }
            return {x * mesh.vertices[t[0]].cast<double>() + y * mesh.vertices[t[1]].cast<double>() + z * mesh.vertices[t[2]].cast<double>(),
                    (x * normals[t[0]] + y * normals[t[1]] + z * normals[t[2]]).normalized()};
        }
        throw Issue::NotHeightField;
    }
};
Patch parameterize(const Input& input, const FormorixSurfaces::Settings& s)
{
    Patch p;
    p.mesh = weld(input.surface);
    if (p.mesh.indices.empty())
        throw Issue::Empty;
    if (p.mesh.indices.size() > 40000)
        throw Issue::TooLarge;
    p.normals.assign(p.mesh.vertices.size(), Vec3d::Zero());
    using Edge = std::pair<int, int>;
    std::map<Edge, std::vector<int>> edges;
    Vec3d sum = Vec3d::Zero();
    for (std::size_t i = 0; i < p.mesh.indices.size(); ++i) {
        const auto& t = p.mesh.indices[i];
        const Vec3d n = (p.mesh.vertices[t[1]] - p.mesh.vertices[t[0]]).cross(p.mesh.vertices[t[2]] - p.mesh.vertices[t[0]]).cast<double>();
        if (n.norm() < eps)
            throw Issue::InvalidMesh;
        sum += n;
        for (int j = 0; j < 3; ++j) {
            p.normals[t[j]] += n;
            edges[std::minmax(t[j], t[(j + 1) % 3])].push_back(int(i));
        }
    }
    if (sum.norm() < eps)
        throw Issue::NotHeightField;
    p.axis = sum.normalized();
    for (auto& n : p.normals) {
        if (n.norm() < eps)
            throw Issue::InvalidMesh;
        n.normalize();
    }
    std::vector<std::vector<int>> adjacency(p.mesh.indices.size());
    std::map<int, int> next;
    for (const auto& [edge, faces] : edges) {
        if (faces.size() > 2)
            throw Issue::NonDisk;
        if (faces.size() == 2) {
            adjacency[faces[0]].push_back(faces[1]);
            adjacency[faces[1]].push_back(faces[0]);
        } else {
            const auto& t = p.mesh.indices[faces[0]];
            for (int j = 0; j < 3; ++j)
                if (std::minmax(t[j], t[(j + 1) % 3]) == std::minmax(edge.first, edge.second)) {
                    if (!next.emplace(t[j], t[(j + 1) % 3]).second)
                        throw Issue::NonDisk;
                }
        }
    }
    std::set<int> visited{0};
    std::queue<int> todo;
    todo.push(0);
    while (!todo.empty()) {
        int i = todo.front();
        todo.pop();
        for (int j : adjacency[i])
            if (visited.insert(j).second)
                todo.push(j);
    }
    if (visited.size() != p.mesh.indices.size())
        throw Issue::Disconnected;
    if (next.size() < 3 || p.mesh.vertices.size() + p.mesh.indices.size() != edges.size() + 1)
        throw Issue::NonDisk;
    int current = next.begin()->first;
    do {
        p.boundary.push_back(current);
        if (!next.count(current))
            throw Issue::NonDisk;
        current = next.at(current);
    } while (current != p.boundary.front() && p.boundary.size() <= next.size());
    if (p.boundary.size() != next.size())
        throw Issue::NonDisk;
    p.u = std::abs(p.axis.x()) < .8 ? Vec3d::UnitX() : Vec3d::UnitY();
    if (s.direction == FormorixSurfaces::Direction::Custom)
        p.u = Vec3d(s.custom_direction[0], s.custom_direction[1], s.custom_direction[2]);
    if (s.direction == FormorixSurfaces::Direction::Edge && s.edge_selected)
        p.u = Vec3d(s.edge[3] - s.edge[0], s.edge[4] - s.edge[1], s.edge[5] - s.edge[2]);
    p.u -= p.axis * p.u.dot(p.axis);
    if (p.u.norm() < eps)
        throw Issue::NotHeightField;
    p.u.normalize();
    p.v  = p.axis.cross(p.u);
    for (int a = 0; a < 3; ++a)
        if (std::abs(p.axis[a]) == 1. &&
            std::all_of(p.mesh.vertices.begin(), p.mesh.vertices.end(),
                        [&](const Vec3f& vertex) { return vertex[a] == p.mesh.vertices.front()[a]; }))
            p.constant_axis = a;
    p.lo = Vec2d::Constant(std::numeric_limits<double>::max());
    p.hi = -p.lo;
    for (const auto& v : p.mesh.vertices) {
        Vec2d uv(v.cast<double>().dot(p.u), v.cast<double>().dot(p.v));
        p.uv.push_back(uv);
        p.lo = p.lo.cwiseMin(uv);
        p.hi = p.hi.cwiseMax(uv);
    }
    for (const auto& t : p.mesh.indices)
        if (cross(p.uv[t[1]] - p.uv[t[0]], p.uv[t[2]] - p.uv[t[0]]) < eps)
            throw Issue::NotHeightField;
    for (int i : p.boundary)
        p.outline.push_back(p.uv[i]);
    simple_boundary(p.outline);
    p.cells = std::clamp(int(std::sqrt(double(p.mesh.indices.size()) / 4.)), 1, 128);
    p.grid.resize(p.cells * p.cells);
    for (std::size_t i = 0; i < p.mesh.indices.size(); ++i) {
        const auto& t = p.mesh.indices[i];
        Vec2d a = p.uv[t[0]].cwiseMin(p.uv[t[1]]).cwiseMin(p.uv[t[2]]), b = p.uv[t[0]].cwiseMax(p.uv[t[1]]).cwiseMax(p.uv[t[2]]);
        const int first = p.cell(a), last = p.cell(b);
        for (int y = first / p.cells; y <= last / p.cells; ++y)
            for (int x = first % p.cells; x <= last % p.cells; ++x)
                p.grid[y * p.cells + x].push_back(int(i));
    }
    return p;
}
Vec3d nozzle_axis(const Vec3d& normal, const FormorixSurfaces::Settings& s, double limit)
{
    const double tilt = std::acos(std::clamp(normal.z(), -1., 1.)) * 180. / pi;
    if (tilt >= 89.9 && !s.allow_vertical)
        throw Issue::VerticalDisabled;
    const double deviation = s.allow_deviation ? s.max_deviation : 0.;
    if (tilt > limit + deviation + 1e-5)
        throw Issue::TiltExceeded;
    return tilt <= limit ? normal : interpolate_axis(normal, Vec3d::UnitZ(), (tilt - limit) / tilt);
}
std::vector<Vec2d> raster(const Patch& p, double width, double margin, double longitudinal_spacing, bool ribbon, const std::function<bool()>& cancel)
{
    std::vector<Vec2d> boundary = p.outline;
    if (margin > 0.) {
        Polygon polygon;
        for (const auto& v : boundary)
            polygon.points.emplace_back(scale_(v.x()), scale_(v.y()));
        const auto inset = offset(polygon, -scale_(margin));
        if (inset.size() != 1 || inset.front().points.size() < 3)
            throw Issue::NonDisk;
        boundary.clear();
        for (const auto& v : inset.front().points)
            boundary.emplace_back(unscale<double>(v.x()), unscale<double>(v.y()));
    }
    double low = std::numeric_limits<double>::max(), high = -low;
    for (const auto& v : boundary) {
        low  = std::min(low, v.y());
        high = std::max(high, v.y());
    }
    double steep = 1.;
    for (const auto& n : p.normals)
        steep = std::min(steep, n.dot(p.axis));
    if (steep < .1)
        throw Issue::NotHeightField;
    const int rows = ribbon ? int(std::floor((high - low) / width + 1e-8)) : std::max(1, int(std::ceil((high - low) / (width * steep))));
    if (rows < 1)
        throw Issue::ThinWall;
    if (rows > 2000)
        throw Issue::TooLarge;
    const double sample = longitudinal_spacing > 0. ? longitudinal_spacing : std::min(.5, width);
    std::vector<Vec2d> path;
    auto line = [&](const Vec2d& a, const Vec2d& b) {
        const int n = std::max(1, int(std::ceil((b - a).norm() / sample)));
        if (path.size() + n > 100000)
            throw Issue::TooLarge;
        if (path.empty())
            path.push_back(a);
        for (int i = 1; i <= n; ++i)
            path.push_back(a + (b - a) * (double(i) / n));
    };
    struct Hit
    {
        Vec2d p;
        int edge;
    };
    Hit previous;
    for (int row = 0; row < rows; ++row) {
        if (cancel && cancel())
            throw Issue::Canceled;
        const double y = low + (ribbon ? width : (high - low) / rows) * (row + .5);
        std::vector<Hit> hits;
        for (int i = 0; i < int(boundary.size()); ++i) {
            const Vec2d a = boundary[i], b = boundary[(i + 1) % boundary.size()];
            if ((a.y() <= y && b.y() > y) || (b.y() <= y && a.y() > y))
                hits.push_back({a + (b - a) * ((y - a.y()) / (b.y() - a.y())), i});
        }
        if (hits.size() != 2)
            throw Issue::NotHeightField;
        std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.p.x() < b.p.x(); });
        if (row % 2)
            std::swap(hits[0], hits[1]);
        if (ribbon) {
            const Vec2d direction = (hits[1].p - hits[0].p).normalized();
            if ((hits[1].p - hits[0].p).norm() < width)
                throw Issue::ThinWall;
            const Vec2d start = hits[0].p + (row ? width / 2. : 0.) * direction;
            const Vec2d end = hits[1].p - (row + 1 < rows ? width / 2. : 0.) * direction;
            if (row)
                line(previous.p, start);
            line(start, end);
            previous.p = end;
            continue;
        }
        if (row > 0) {
            // Join rows along the actual selected boundary, not across an unselected gap.
            std::vector<Vec2d> candidates[2];
            for (int direction = 0; direction < 2; ++direction) {
                auto& c = candidates[direction];
                c.push_back(previous.p);
                if (previous.edge != hits[0].edge) {
                    int edge = previous.edge;
                    while (edge != hits[0].edge) {
                        if (direction == 0) {
                            c.push_back(boundary[(edge + 1) % boundary.size()]);
                            edge = (edge + 1) % boundary.size();
                        } else {
                            c.push_back(boundary[edge]);
                            edge = (edge + int(boundary.size()) - 1) % boundary.size();
                        }
                    }
                }
                c.push_back(hits[0].p);
            }
            auto length = [](const std::vector<Vec2d>& v) {
                double d = 0.;
                for (std::size_t i = 1; i < v.size(); ++i)
                    d += (v[i] - v[i - 1]).norm();
                return d;
            };
            const auto& c = length(candidates[0]) <= length(candidates[1]) ? candidates[0] : candidates[1];
            for (std::size_t i = 1; i < c.size(); ++i)
                line(c[i - 1], c[i]);
        }
        line(hits[0].p, hits[1].p);
        previous = hits[1];
    }
    return path;
}
std::vector<Vec2d> follow_facets(const Patch& p, const std::vector<Vec2d>& path, const std::function<bool()>& cancel)
{
    // Insert facet crossings without introducing a travel, stop or extrusion break.
    // Thus outer segments follow the actual mesh, including concave triangulated surfaces.
    std::vector<Vec2d> result;
    if (path.empty())
        return result;
    result.push_back(path.front());
    for (std::size_t i = 1; i < path.size(); ++i) {
        if (cancel && cancel())
            throw Issue::Canceled;
        const Vec2d a = path[i - 1], b = path[i], delta = b - a;
        if (delta.norm() < eps)
            continue;
        std::vector<double> splits{1.};
        const int first = p.cell(a.cwiseMin(b)), last = p.cell(a.cwiseMax(b));
        for (int y = first / p.cells; y <= last / p.cells; ++y)
            for (int x = first % p.cells; x <= last % p.cells; ++x)
                for (int triangle : p.grid[y * p.cells + x]) {
                    const auto& t = p.mesh.indices[triangle];
                    for (int j = 0; j < 3; ++j) {
                        const Vec2d c = p.uv[t[j]], edge = p.uv[t[(j + 1) % 3]] - c;
                        const double divisor = cross(delta, edge);
                        if (std::abs(divisor) < 1e-12)
                            continue;
                        const double along = cross(c - a, edge) / divisor, across = cross(c - a, delta) / divisor;
                        if (along > 1e-8 && along < 1. - 1e-8 && across >= -1e-8 && across <= 1. + 1e-8)
                            splits.push_back(along);
                    }
                }
        std::sort(splits.begin(), splits.end());
        double previous = -1.;
        for (double f : splits)
            if (f - previous > 1e-8) {
                result.push_back(a + f * delta);
                previous = f;
            }
        if (result.size() > 100000)
            throw Issue::TooLarge;
    }
    return result;
}
} // namespace

Vec3d interpolate_axis(const Vec3d& a, const Vec3d& b, double fraction)
{
    const double f                    = std::clamp(fraction, 0., 1.);
    const Eigen::Quaterniond rotation = Eigen::Quaterniond::FromTwoVectors(a, b);
    return (Eigen::Quaterniond::Identity().slerp(f, rotation) * a).normalized();
}
double Timeline::duration() const
{
    double end = 0.;
    for (const auto& h : heads)
        if (!h.empty())
            end = std::max(end, h.back().end);
    return end;
}
bool Timeline::valid() const
{
    for (int h = 0; h < 3; ++h) {
        double end = 0.;
        for (const auto& m : heads[h]) {
            if (m.head != h || !std::isfinite(m.start) || !std::isfinite(m.end) || m.start < end || m.end <= m.start ||
                !finite(m.from.position) || !finite(m.to.position) || !finite(m.from.axis) || !finite(m.to.axis) ||
                std::abs(m.from.axis.norm() - 1.) > 1e-5 || std::abs(m.to.axis.norm() - 1.) > 1e-5 ||
                m.extrusion != (m.kind == MotionKind::Extrude) || (m.process && !FormorixProcess::valid(*m.process)) ||
                !std::isfinite(m.mm3_per_mm) || m.mm3_per_mm<0. || (!m.extrusion && m.mm3_per_mm!=0.))
                return false;
            end = m.end;
        }
    }
    return true;
}
HeadState Timeline::sample(int head, double seconds) const
{
    if (head < 0 || head > 2 || !std::isfinite(seconds))
        throw std::invalid_argument("Invalid preview time/head");
    const auto& track = heads[head];
    HeadState state;
    state.pose = idle[head];
    if (track.empty())
        return state;
    auto it = std::upper_bound(track.begin(), track.end(), seconds, [](double t, const Motion& m) { return t < m.end; });
    if (it == track.end()) {
        state.pose    = track.back().to;
        state.kind    = MotionKind::Wait;
        state.segment = track.size();
        state.process = track.back().process;
        return state;
    }
    state.segment = std::size_t(it - track.begin());
    state.process = it->process;
    if (seconds < it->start) {
        state.pose = it == track.begin() ? it->from : (it - 1)->to;
        state.process = it == track.begin() ? it->process : (it - 1)->process;
        state.kind = MotionKind::Wait;
        return state;
    }
    const double f  = std::clamp((seconds - it->start) / (it->end - it->start), 0., 1.);
    state.pose      = {it->from.position + (it->to.position - it->from.position) * f, interpolate_axis(it->from.axis, it->to.axis, f)};
    state.kind      = it->kind;
    state.extrusion = it->extrusion;
    return state;
}
Motion process_motion(int head, int region, const FormorixProcess::Profile& profile, const Pose& from, const Pose& to, double start)
{
    const double distance = (to.position - from.position).norm();
    if (head < 0 || head > 2 || region <= 0 || !std::isfinite(start) || start < 0. ||
        !from.position.allFinite() || !to.position.allFinite() || !from.axis.allFinite() || !to.axis.allFinite() ||
        std::abs(from.axis.norm()-1.) > 1e-8 || std::abs(to.axis.norm()-1.) > 1e-8 || distance < 1e-9)
        throw std::invalid_argument("Invalid process motion");
    return {head, start, start + distance / FormorixProcess::effective_speed(profile), from, to,
            MotionKind::Extrude, true, 0, region, profile};
}
Input extract(const ModelObject& object, int instance, int region)
{
    if (instance < 0 || instance >= int(object.instances.size()))
        throw std::invalid_argument("Invalid instance");
    Input input;
    for (const ModelVolume* v : object.volumes) {
        if (!v->is_model_part())
            continue;
        if (!FormorixSurfaces::painting_matches(*v))
            throw Issue::StalePaint;
        const Transform3d transform = object.instances[instance]->get_matrix() * v->get_matrix();
        append(input.solid, v->mesh().its, transform);
        TriangleSelector selected(v->mesh());
        selected.deserialize(v->formorix_facets.get_data());
        append(input.surface, selected.get_facets_strict(EnforcerBlockerType(region)), transform);
        EnforcerBlockerStateMap map;
        for (int i = 0; i <= 32; ++i)
            map[i] = EnforcerBlockerType(i == region ? 0 : 1);
        selected.remap_triangle_state(map);
        append(input.remainder, selected.get_facets_strict(EnforcerBlockerType(1)), transform);
    }
    return input;
}
Vec3d reference_direction(const ModelObject& object, int instance, const FormorixSurfaces::Settings& s)
{
    if (instance < 0 || instance >= int(object.instances.size()) || !s.edge_selected)
        throw Issue::ReferenceEdge;
    const Vec3d a(s.edge[0], s.edge[1], s.edge[2]), b(s.edge[3], s.edge[4], s.edge[5]);
    const ModelVolume* source = nullptr;
    for (const auto* v : object.volumes)
        if (v->is_model_part()) {
            bool has_a = false, has_b = false;
            for (const auto& p : v->mesh().its.vertices) {
                has_a |= (p.cast<double>() - a).norm() < 1e-5;
                has_b |= (p.cast<double>() - b).norm() < 1e-5;
            }
            if (has_a && has_b) {
                if (source)
                    throw Issue::ReferenceEdge;
                source = v;
            }
        }
    if (!source || (b - a).norm() < eps)
        throw Issue::ReferenceEdge;
    return (object.instances[instance]->get_matrix() * source->get_matrix()).linear() * (b - a);
}
Result generate(const Input& input, const FormorixSurfaces::Settings& s, const Options& o, const std::function<bool()>& cancel)
{
    Result result;
    result.selected = input.surface;
    try {
        if (o.head < 0 || o.head > 2 || !std::isfinite(o.line_width) || o.line_width < .05 || !std::isfinite(o.speed) || o.speed <= 0. ||
            !std::isfinite(o.travel_speed) || o.travel_speed <= 0. || !std::isfinite(o.max_tilt) || o.max_tilt <= 0. || o.max_tilt > 180. ||
            !std::isfinite(o.longitudinal_spacing) || o.longitudinal_spacing < 0. || o.longitudinal_spacing > .5)
            throw Issue::InvalidMesh;
        FormorixSurfaces::Project validation;
        validation.regions = {{o.region, "Preview", true, false, s}};
        validation.encode();
        if (cancel && cancel())
            throw Issue::Canceled;
        Patch patch      = parameterize(input, s);
        const auto solid = weld(input.solid);
        if (solid.indices.empty())
            throw Issue::ThinWall;
        if (solid.indices.size() > 500000)
            throw Issue::TooLarge;
        std::map<std::pair<int, int>, std::pair<int, int>> solid_edges;
        for (const auto& t : solid.indices)
            for (int j = 0; j < 3; ++j) {
                int a = t[j], b = t[(j + 1) % 3];
                auto& e = solid_edges[std::minmax(a, b)];
                ++e.first;
                e.second += a < b ? 1 : -1;
            }
        for (const auto& entry : solid_edges)
            if (entry.second.first != 2 || entry.second.second != 0)
                throw Issue::InvalidMesh;
        WallRaycaster raycaster(solid);
        double minimum = std::numeric_limits<double>::max(), maximum = 0.;
        std::vector<double> thickness(patch.mesh.vertices.size());
        for (std::size_t i = 0; i < patch.mesh.vertices.size(); ++i) {
            if (cancel && cancel())
                throw Issue::Canceled;
            const Vec3d point = patch.mesh.vertices[i].cast<double>(), normal = patch.normals[i];
            nozzle_axis(normal, s, o.max_tilt);
            thickness[i] = o.validate_wall ? raycaster.thickness(point, normal) : std::numeric_limits<double>::infinity();
            minimum      = std::min(minimum, thickness[i]);
            maximum      = std::max(maximum, thickness[i]);
        }
        result.full_wall = s.shell_mode == FormorixSurfaces::ShellMode::FullWall;
        if (result.full_wall && maximum - minimum > std::max(.02, s.layer_height * .1))
            throw Issue::FullWallVariable;
        std::vector<double> depths;
        const double requested = result.full_wall ? minimum : s.shell_thickness;
        double depth           = 0.;
        for (int i = 0;; ++i) {
            if (s.shell_mode == FormorixSurfaces::ShellMode::Layers ? i >= s.layers : depth >= requested - 1e-6)
                break;
            if (i >= 200)
                throw Issue::TooLarge;
            depths.push_back(depth);
            depth += i < s.fine_final_layers ? s.fine_layer_height : s.layer_height;
        }
        if (depths.empty())
            throw Issue::Empty;
        result.shell_depth = s.shell_mode == FormorixSurfaces::ShellMode::Layers ? depth : requested;
        if (result.shell_depth > minimum + 1e-4)
            throw Issue::ThinWall;
        const double longitudinal = o.longitudinal_spacing>0. ? o.longitudinal_spacing :
            std::min(.5,o.line_width*std::array<double,3>{2.,1.,.5}[s.quality]);
        auto path2d = follow_facets(patch, raster(patch, o.line_width, s.boundary_margin, longitudinal, o.compacted_ribbon, cancel), cancel);
        if (path2d.size() * depths.size() > o.max_segments)
            throw Issue::TooLarge;
        std::vector<Pose> outer;
        outer.reserve(path2d.size());
        for (const auto& point : path2d) {
            const auto pose = patch.at(point);
            if (o.validate_wall && raycaster.thickness(pose.position, pose.axis) < result.shell_depth - 1e-4)
                throw Issue::ThinWall;
            outer.push_back(pose);
        }
        auto offset_mesh = [&](double d) {
            auto mesh = patch.mesh;
            for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
                mesh.vertices[i] = (mesh.vertices[i].cast<double>() - d * patch.normals[i]).cast<float>();
            std::vector<Vec2d> outline;
            for (int i : patch.boundary) {
                Vec3d v = mesh.vertices[i].cast<double>();
                outline.emplace_back(v.dot(patch.u), v.dot(patch.v));
            }
            simple_boundary(outline);
            for (const auto& t : mesh.indices) {
                const Vec3d n = (mesh.vertices[t[1]] - mesh.vertices[t[0]]).cross(mesh.vertices[t[2]] - mesh.vertices[t[0]]).cast<double>();
                const Vec3d old = (patch.mesh.vertices[t[1]] - patch.mesh.vertices[t[0]])
                                      .cross(patch.mesh.vertices[t[2]] - patch.mesh.vertices[t[0]])
                                      .cast<double>();
                if (n.dot(patch.axis) <= eps || n.dot(old) <= 0.)
                    throw Issue::InvalidOffset;
            }
            return mesh;
        };
        // Build inner-to-outer. The outer TCP envelope lies on, never outside, the selected mesh.
        for (auto it = depths.rbegin(); it != depths.rend(); ++it) {
            if (cancel && cancel())
                throw Issue::Canceled;
            Layer layer;
            layer.depth   = *it;
            layer.surface = offset_mesh(*it);
            for (const auto& pose : outer)
                layer.path.push_back({pose.position - *it * pose.axis, nozzle_axis(pose.axis, s, o.max_tilt)});
            if (s.alternate_directions && result.layers.size() % 2)
                std::reverse(layer.path.begin(), layer.path.end());
            result.layers.push_back(std::move(layer));
        }
        if (!result.full_wall) {
            result.core      = input.remainder;
            const auto inner = offset_mesh(result.shell_depth);
            append(result.core, inner, Transform3d::Identity());
            for (std::size_t j = 0; j < patch.boundary.size(); ++j) {
                int a = patch.boundary[j], b = patch.boundary[(j + 1) % patch.boundary.size()], base = int(result.core.vertices.size());
                result.core.vertices.insert(result.core.vertices.end(),
                                            {patch.mesh.vertices[a], patch.mesh.vertices[b], inner.vertices[b], inner.vertices[a]});
                result.core.indices.emplace_back(base, base + 2, base + 1);
                result.core.indices.emplace_back(base, base + 3, base + 2);
            }
        }
        double time = 0.;
        auto& track = result.timeline.heads[o.head];
        auto move   = [&](Pose a, Pose b, int layer, bool extrude) {
            double distance = (b.position - a.position).norm();
            if (distance < eps)
                return;
            double end = time + distance / (extrude ? o.speed : o.travel_speed);
            track.push_back({o.head, time, end, a, b, extrude ? MotionKind::Extrude : MotionKind::Travel, extrude, layer, o.region});
            time = end;
        };
        for (std::size_t i = 0; i < result.layers.size(); ++i) {
            const auto& path = result.layers[i].path;
            if (!track.empty())
                move(track.back().to, path.front(), int(i), false);
            for (std::size_t j = 1; j < path.size(); ++j)
                move(path[j - 1], path[j], int(i), true);
        }
        for (int i = 0; i < 3; ++i)
            result.timeline.idle[i] = {outer.front().position + Vec3d(-4. - i * 4., 0., 5.), Vec3d::UnitZ()};
        if (!result.timeline.valid())
            throw Issue::InvalidMesh;
    } catch (Issue issue) {
        result.errors.push_back(issue);
        result.layers.clear();
        result.timeline = Timeline{};
        result.core     = {};
    } catch (const std::exception&) {
        result.errors.push_back(Issue::InvalidMesh);
        result.layers.clear();
        result.timeline = Timeline{};
        result.core     = {};
    }
    return result;
}
} // namespace Slic3r::FormorixPreview
