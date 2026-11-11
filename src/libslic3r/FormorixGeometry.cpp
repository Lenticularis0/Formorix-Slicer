#include "FormorixGeometry.hpp"
#include "FormorixGeometryInternal.hpp"
#include "I18N.hpp"
#include "Point.hpp"
#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Slic3r::FormorixGeometry {
// Keep the CGAL archive independent of libslic3r to avoid a static-library
// link cycle on platforms with order-sensitive archive resolution.
Partition partition_meshes(const Mesh&, const std::vector<std::pair<int, Mesh>>&, bool clip);
namespace {
void check_mesh(const Mesh& m)
{
    if (m.faces.empty() || m.faces.size() > 200000 || m.vertices.size() > 200000)
        throw std::invalid_argument("Empty mesh or geometry resource limit");
    for (const auto& p : m.vertices)
        if (!p.allFinite() || p.cwiseAbs().maxCoeff() > 1e7)
            throw std::invalid_argument("Invalid mesh coordinate");
    for (const auto& f : m.faces) {
        for (int i : f)
            if (i < 0 || std::size_t(i) >= m.vertices.size())
                throw std::invalid_argument("Invalid vertex index");
        if ((m.vertices[f[1]] - m.vertices[f[0]]).cross(m.vertices[f[2]] - m.vertices[f[0]]).norm() < 1e-14)
            throw std::invalid_argument("Degenerate face");
    }
}
struct EdgeUse
{
    int face, from, to;
};
using Edges = std::map<std::pair<int, int>, std::vector<EdgeUse>>;
Edges edges(const Mesh& m)
{
    check_mesh(m);
    Edges out;
    for (std::size_t f = 0; f < m.faces.size(); ++f)
        for (int k = 0; k < 3; ++k) {
            int a = m.faces[f][k], b = m.faces[f][(k + 1) % 3];
            auto& uses = out[std::minmax(a, b)];
            if (uses.size() == 2 || (!uses.empty() && uses.front().from == a))
                throw std::invalid_argument("Non-manifold or inconsistently oriented patch");
            uses.push_back({int(f), a, b});
        }
    return out;
}
double segment_distance(const Vec3d& p, const Vec3d& a, const Vec3d& b)
{
    const Vec3d d = b - a;
    return (p - a - std::clamp((p - a).dot(d) / d.squaredNorm(), 0., 1.) * d).norm();
}
} // namespace

namespace {
using VertexKey = std::array<double, 3>;
VertexKey vertex_key(const Vec3d& p) { return {p.x(), p.y(), p.z()}; }
struct FoldedOffset : std::invalid_argument
{
    FoldedOffset() : std::invalid_argument(_u8L("Folded offset or reversed normal")) {}
};
Mesh shell_cutter_impl(const ShellRequest& r, const std::vector<Vec3d>* inner = nullptr)
{
    const auto boundary = edges(r.surface);
    const std::size_t n = r.surface.vertices.size();
    if (r.region <= 0 || r.depths.size() != n || r.outward_normals.size() != n)
        throw std::invalid_argument("Invalid shell request");
    Mesh out = r.surface;
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(r.depths[i]) || r.depths[i] <= 0. || r.depths[i] > 10000. || !r.outward_normals[i].allFinite() ||
            std::abs(r.outward_normals[i].norm() - 1.) > 1e-8)
            throw std::invalid_argument("Invalid shell depth/normal");
        out.vertices.push_back(inner ? inner->at(i) : r.surface.vertices[i] - r.depths[i] * r.outward_normals[i]);
    }
    for (const auto& f : r.surface.faces) {
        const Vec3d original = (out.vertices[f[1]] - out.vertices[f[0]]).cross(out.vertices[f[2]] - out.vertices[f[0]]);
        const Vec3d inner    = (out.vertices[f[1] + n] - out.vertices[f[0] + n]).cross(out.vertices[f[2] + n] - out.vertices[f[0] + n]);
        if (original.dot(inner) <= 0. || original.dot(r.outward_normals[f[0]] + r.outward_normals[f[1]] + r.outward_normals[f[2]]) <= 0.)
            throw FoldedOffset();
        out.faces.emplace_back(f[2] + int(n), f[1] + int(n), f[0] + int(n));
    }
    for (const auto& entry : boundary)
        if (entry.second.size() == 1) {
            const auto& e = entry.second.front();
            // Both regions must choose the SAME diagonal on a nonplanar
            // miter quad, despite traversing the shared edge oppositely.
            if (!inner || vertex_key(out.vertices[e.from]) < vertex_key(out.vertices[e.to])) {
                out.faces.emplace_back(e.to, e.from, e.from + int(n));
                out.faces.emplace_back(e.to, e.from + int(n), e.to + int(n));
            } else {
                out.faces.emplace_back(e.to, e.from, e.to + int(n));
                out.faces.emplace_back(e.from, e.from + int(n), e.to + int(n));
            }
        }
    return out;
}

struct SeamUse
{
    std::size_t region;
    int from, to;
};
struct MiteredOffsets
{
    std::vector<std::vector<Vec3d>> inner;
    std::vector<detail::PlanarMiterSeam> seams;
    std::size_t edges = 0, vertices = 0;
    double residual = 0.;
};
// Inputs have already passed shell_cutter's shape/normal/depth validation.
MiteredOffsets miter_offsets(const std::vector<ShellRequest>& requests)
{
    MiteredOffsets out;
    std::map<std::pair<VertexKey, VertexKey>, std::vector<SeamUse>> boundary;
    for (std::size_t region = 0; region < requests.size(); ++region) {
        const auto& r = requests[region];
        out.inner.emplace_back();
        for (std::size_t i = 0; i < r.surface.vertices.size(); ++i)
            out.inner.back().push_back(r.surface.vertices[i] - r.depths[i] * r.outward_normals[i]);
        for (const auto& entry : edges(r.surface))
            if (entry.second.size() == 1) {
                const auto& e = entry.second.front();
                const auto a = vertex_key(r.surface.vertices[e.from]), b = vertex_key(r.surface.vertices[e.to]);
                boundary[std::minmax(a, b)].push_back({region, e.from, e.to});
            }
    }
    std::map<VertexKey, std::set<std::pair<std::size_t, int>>> junctions;
    for (const auto& entry : boundary) {
        const auto& uses = entry.second;
        if (uses.size() < 2)
            continue;
        if (uses.size() != 2 || uses[0].region == uses[1].region)
            throw std::invalid_argument("Shared shell edge is not a two-region boundary");
        const auto& a = requests[uses[0].region];
        const auto& b = requests[uses[1].region];
        // Adjacent outward-oriented patches traverse a common edge oppositely.
        if (vertex_key(a.surface.vertices[uses[0].from]) != vertex_key(b.surface.vertices[uses[1].to]))
            throw std::invalid_argument("Shared shell boundary has inconsistent surface orientation");
        const bool parallel = (a.outward_normals[uses[0].from] - b.outward_normals[uses[1].to]).norm() < 1e-8 &&
                              (a.outward_normals[uses[0].to] - b.outward_normals[uses[1].from]).norm() < 1e-8;
        // Different depths on a flat surface form a step, not a miter.
        if (parallel)
            continue;
        out.seams.push_back({uses[0].region, uses[1].region, {uses[0].from, uses[0].to}, {}});
        ++out.edges;
        for (const auto& use : uses)
            for (int vertex : {use.from, use.to})
                junctions[vertex_key(requests[use.region].surface.vertices[vertex])].insert({use.region, vertex});
    }
    for (const auto& junction : junctions) {
        struct Constraint
        {
            Vec3d normal;
            double depth;
        };
        std::vector<Constraint> constraints;
        double maximum_depth = 0.;
        for (const auto& use : junction.second) {
            const auto& r = requests[use.first];
            constraints.push_back({r.outward_normals[use.second], r.depths[use.second]});
            maximum_depth = std::max(maximum_depth, r.depths[use.second]);
        }
        // Geometric ordering makes the construction independent of region IDs
        // and request order, including corners shared by three regions.
        std::sort(constraints.begin(), constraints.end(), [](const auto& a, const auto& b) {
            return std::array<double, 4>{a.normal.x(), a.normal.y(), a.normal.z(), a.depth} <
                   std::array<double, 4>{b.normal.x(), b.normal.y(), b.normal.z(), b.depth};
        });
        std::vector<Vec3d> basis;
        Vec3d offset = Vec3d::Zero();
        for (const auto& constraint : constraints) {
            Vec3d direction = constraint.normal;
            for (const auto& axis : basis)
                direction -= direction.dot(axis) * axis;
            const double rhs = constraint.depth - constraint.normal.dot(offset);
            if (direction.squaredNorm() < 1e-16) {
                if (std::abs(rhs) > 1e-8 * std::max(1., maximum_depth))
                    throw std::invalid_argument("Incompatible offset planes at shared shell junction");
            } else {
                direction.normalize();
                offset += direction * (rhs / constraint.normal.dot(direction));
                basis.push_back(direction);
            }
        }
        if (!offset.allFinite() || offset.norm() > 4. * maximum_depth)
            throw std::invalid_argument("Shared shell junction exceeds the geometric miter limit");
        const Vec3d p(junction.first[0], junction.first[1], junction.first[2]);
        const Vec3d inner = p - offset;
        for (const auto& constraint : constraints) {
            const double residual = std::abs(constraint.normal.dot(p - inner) - constraint.depth);
            out.residual          = std::max(out.residual, residual);
            if (residual > 1e-8 * std::max(1., maximum_depth))
                throw std::invalid_argument("Shared shell offset constraint could not be resolved");
        }
        for (const auto& use : junction.second)
            out.inner[use.first][use.second] = inner;
        ++out.vertices;
    }
    for (auto& seam : out.seams)
        seam.inner = {out.inner[seam.first][seam.vertices[0]], out.inner[seam.first][seam.vertices[1]]};
    return out;
}
} // namespace

Mesh shell_cutter(const ShellRequest& r) { return shell_cutter_impl(r); }

Partition partition(const Mesh& solid, const std::vector<ShellRequest>& requests)
{
    try {
        if (requests.empty() || requests.size() > 32)
            throw std::invalid_argument("Invalid region count");
        std::vector<std::pair<int, Mesh>> cutters;
        for (const auto& request : requests)
            cutters.emplace_back(request.region, shell_cutter(request));
        return partition_meshes(solid, cutters, false);
    } catch (const std::exception& e) {
        Partition out;
        out.error = e.what();
        return out;
    }
}

Partition partition_clipped(const Mesh& solid, const std::vector<ShellRequest>& requests, SharedBoundaryPolicy policy)
{
    try {
        if (requests.empty() || requests.size() > 32)
            throw std::invalid_argument("Invalid region count");
        std::vector<std::pair<int, Mesh>> cutters;
        for (const auto& request : requests)
            cutters.emplace_back(request.region, shell_cutter(request));
        MiteredOffsets miter;
        Partition out;
        if (policy == SharedBoundaryPolicy::MiterAdjacentEdges) {
            miter = miter_offsets(requests);
            auto mitered = cutters;
            try {
                for (std::size_t i = 0; i < requests.size(); ++i)
                    mitered[i].second = shell_cutter_impl(requests[i], &miter.inner[i]);
                out = partition_meshes(solid, mitered, true);
            } catch (const FoldedOffset&) {
                // A valid prism may have a finer first row than its tangential
                // miter displacement. Do not move/flip that row or the CAD skin.
                out = detail::partition_planar_miters(solid, requests, cutters, miter.seams);
            }
        } else
            out = partition_meshes(solid, cutters, true);
        out.mitered_edges             = miter.edges;
        out.mitered_vertices          = miter.vertices;
        out.miter_constraint_residual = miter.residual;
        return out;
    } catch (const std::exception& e) {
        Partition out;
        out.error = e.what();
        return out;
    }
}

bool Deviation::within(double tolerance) const
{ return std::isfinite(tolerance) && tolerance > 0. && bounded && std::isfinite(upper_bound) && upper_bound <= tolerance; }
Reference plane(const Vec3d& origin, const Vec3d& normal)
{
    if (!origin.allFinite() || !normal.allFinite() || normal.norm() < 1e-12)
        throw std::invalid_argument("Invalid plane");
    const Vec3d n = normal.normalized();
    return {[origin, n](const Vec3d& p) { return (p - origin).dot(n); }, [](const Vec3d&, const Vec3d&, const Vec3d&) { return 0.; },
            "analytic plane"};
}
Reference sphere(const Vec3d& center, double radius)
{
    if (!center.allFinite() || !std::isfinite(radius) || radius <= 0.)
        throw std::invalid_argument("Invalid sphere");
    return {[center, radius](const Vec3d& p) { return (p - center).norm() - radius; },
            [center](const Vec3d& a, const Vec3d& b, const Vec3d& c) {
                // Every point is in the ball centred at a with radius max(|b-a|,|c-a|).
                const double low = (a - center).norm() - std::max((b - a).norm(), (c - a).norm());
                return low > 0. ? 1. / low : std::numeric_limits<double>::infinity();
            },
            "analytic sphere"};
}
Reference cylinder(const Vec3d& origin, const Vec3d& axis, double radius)
{
    if (!origin.allFinite() || !axis.allFinite() || axis.norm() < 1e-12 || !std::isfinite(radius) || radius <= 0.)
        throw std::invalid_argument("Invalid cylinder");
    const Vec3d n = axis.normalized();
    auto radial   = [origin, n](const Vec3d& p) -> Vec3d {
        Vec3d q = p - origin;
        return q - n * q.dot(n);
    };
    Reference ref{[radial, radius](const Vec3d& p) { return radial(p).norm() - radius; },
                  [radial](const Vec3d& a, const Vec3d& b, const Vec3d& c) {
                      const Vec3d ra   = radial(a);
                      const double low = ra.norm() - std::max((radial(b) - ra).norm(), (radial(c) - ra).norm());
                      return low > 0. ? 1. / low : std::numeric_limits<double>::infinity();
                  },
                  "analytic cylinder"};
    ref.linearization_error = [radial](const Vec3d& a, const Vec3d& b, const Vec3d& c) {
        const Vec3d ra = radial(a), rb = radial(b), rc = radial(c);
        const double diameter = std::max({(ra - rb).norm(), (ra - rc).norm(), (rb - rc).norm()});
        const double low      = ra.norm() - diameter;
        return low > 0. ? .5 * diameter * diameter / low : std::numeric_limits<double>::infinity();
    };
    return ref;
}
Deviation measure(const Mesh& mesh, const Reference& ref, double slack, std::size_t max_leaves)
{
    check_mesh(mesh);
    if (!ref.signed_distance || !std::isfinite(slack) || slack <= 0. || max_leaves < mesh.faces.size())
        throw std::invalid_argument("Invalid accuracy request");
    Deviation out;
    double area_sum = 0., sum = 0., sum2 = 0., upper = 0.;
    std::size_t leaves = mesh.faces.size();
    bool exhausted     = false;
    auto distance      = [&](const Vec3d& p) {
        const double d = std::abs(ref.signed_distance(p));
        if (!std::isfinite(d))
            throw std::invalid_argument("Invalid reference distance");
        ++out.samples;
        out.maximum = std::max(out.maximum, d);
        return d;
    };
    std::function<void(Vec3d, Vec3d, Vec3d, int)> visit;
    visit = [&](Vec3d a, Vec3d b, Vec3d c, int depth) {
        const Vec3d center = (a + b + c) / 3.;
        const double da = distance(a), db = distance(b), dc = distance(c), dm = distance(center);
        const double diameter = std::max({(a - b).norm(), (a - c).norm(), (b - c).norm()});
        const double h        = ref.hessian_bound ? ref.hessian_bound(a, b, c) : std::numeric_limits<double>::infinity();
        if (std::isnan(h) || h < 0.)
            throw std::invalid_argument("Invalid Hessian bound");
        // Taylor remainder for barycentric interpolation, or 1-Lipschitz distance bound.
        const double remainder = ref.linearization_error ? ref.linearization_error(a, b, c) : .5 * h * diameter * diameter;
        if (std::isnan(remainder) || remainder < 0.)
            throw std::invalid_argument("Invalid interpolation remainder");
        const double bound = std::min(std::max({da, db, dc}) + remainder,
                                      dm + std::max({(a - center).norm(), (b - center).norm(), (c - center).norm()}));
        if (bound - std::max({da, db, dc, dm}) > slack && depth < 30 && leaves + 3 <= max_leaves) {
            leaves += 3;
            const Vec3d ab = (a + b) / 2., bc = (b + c) / 2., ca = (c + a) / 2.;
            visit(a, ab, ca, depth + 1);
            visit(ab, b, bc, depth + 1);
            visit(ca, bc, c, depth + 1);
            visit(ab, bc, ca, depth + 1);
            return;
        }
        if (bound - std::max({da, db, dc, dm}) > slack)
            exhausted = true;
        upper             = std::max(upper, bound);
        const double area = .5 * (b - a).cross(c - a).norm();
        area_sum += area;
        sum += dm * area;
        sum2 += dm * dm * area;
    };
    for (const auto& f : mesh.faces)
        visit(mesh.vertices[f[0]], mesh.vertices[f[1]], mesh.vertices[f[2]], 0);
    out.mean        = sum / area_sum;
    out.rms         = std::sqrt(sum2 / area_sum);
    out.upper_bound = upper;
    out.bounded     = !exhausted;
    return out;
}
bool AccuracyBudget::nominal_verified(double tolerance) const
{
    double sum = 0.;
    for (double v : upper_bounds) {
        if (!std::isfinite(v) || v < 0.)
            return false;
        sum += v;
    }
    return std::isfinite(tolerance) && tolerance > 0. && sum <= tolerance;
}
Polyline discretize(const Curve& curve, double first, double last, double tolerance, std::size_t max_segments)
{
    if (!curve.point || !curve.second_derivative_bound || !std::isfinite(first) || !std::isfinite(last) || first >= last ||
        !std::isfinite(tolerance) || tolerance <= 0. || max_segments == 0 || max_segments > 1000000)
        throw std::invalid_argument("Invalid adaptive curve request");
    Polyline out;
    auto point = [&](double t) {
        Vec3d p = curve.point(t);
        if (!p.allFinite())
            throw std::invalid_argument("Invalid curve point");
        return p;
    };
    out.samples.push_back({first, point(first)});
    std::size_t count = 1;
    out.bounded       = true;
    std::function<void(double, double, int)> visit;
    visit = [&](double a, double b, int depth) {
        const double m = curve.second_derivative_bound(a, b);
        if (!std::isfinite(m) || m < 0.)
            throw std::invalid_argument("A finite analytic derivative bound is required");
        const double bound = m * (b - a) * (b - a) / 8.;
        if (bound > tolerance && depth < 40 && count < max_segments) {
            ++count;
            visit(a, (a + b) / 2., depth + 1);
            visit((a + b) / 2., b, depth + 1);
            return;
        }
        out.bounded             = out.bounded && bound <= tolerance;
        out.interpolation_bound = std::max(out.interpolation_bound, bound);
        out.samples.push_back({b, point(b)});
    };
    visit(first, last, 0);
    return out;
}
Atlas make_atlas(const Mesh& m)
{
    Atlas atlas;
    for (const auto& e : edges(m)) {
        if (e.second.size() == 1)
            ++atlas.boundary_edges;
        else
            atlas.portals.push_back({e.second[0].face, e.second[1].face, {e.first.first, e.first.second}});
    }
    return atlas;
}
Vec3d lift(const Mesh& mesh, int face, const Vec2d& uv)
{
    if (face < 0 || std::size_t(face) >= mesh.faces.size() || !uv.allFinite() || uv.x() < 0. || uv.y() < 0. || uv.sum() > 1.)
        throw std::invalid_argument("Outside chart");
    const auto& f = mesh.faces[face];
    return (1. - uv.sum()) * mesh.vertices.at(f[0]) + uv.x() * mesh.vertices.at(f[1]) + uv.y() * mesh.vertices.at(f[2]);
}
std::vector<Vec3d> join_across_charts(const Mesh& m, const Atlas& atlas, const std::vector<PathPiece>& pieces, double tolerance)
{
    check_mesh(m);
    if (pieces.empty() || !std::isfinite(tolerance) || tolerance <= 0.)
        throw std::invalid_argument("Invalid chart route");
    std::vector<Vec3d> path;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const auto& p = pieces[i];
        if (p.face < 0 || std::size_t(p.face) >= m.faces.size() || p.points.size() < 2 || p.head < 0 || p.head > 2 || p.material.empty())
            throw std::invalid_argument("Invalid path piece");
        const auto& f = m.faces[p.face];
        const Vec3d a = m.vertices[f[0]], u = m.vertices[f[1]] - a, v = m.vertices[f[2]] - a;
        const Vec3d cross = u.cross(v);
        const double area = cross.norm();
        const Vec3d n     = cross / area;
        for (const Vec3d& x : p.points) {
            Vec3d d = x - a;
            // Cross-product coordinates avoid cancellation of nearly equal Gram
            // terms on thin triangles. NaN must never bypass a chart boundary.
            double s = d.cross(v).dot(n) / area;
            double t = u.cross(d).dot(n) / area;
            if (!x.allFinite() || !std::isfinite(s) || !std::isfinite(t) || std::abs(d.dot(n)) > tolerance || s < -tolerance ||
                t < -tolerance || s + t > 1. + tolerance)
                throw std::invalid_argument("Path leaves its chart");
        }
        if (i) {
            const auto& prev = pieces[i - 1];
            if (prev.head != p.head || prev.material != p.material || (path.back() - p.points.front()).norm() > tolerance)
                throw std::invalid_argument("Physical continuity requires the same head, material and endpoint");
            if (prev.face != p.face) {
                const auto portal = std::find_if(atlas.portals.begin(), atlas.portals.end(), [&](const Portal& e) {
                    return (e.first_face == prev.face && e.second_face == p.face) || (e.first_face == p.face && e.second_face == prev.face);
                });
                if (portal == atlas.portals.end() ||
                    segment_distance(path.back(), m.vertices.at(portal->edge[0]), m.vertices.at(portal->edge[1])) > tolerance)
                    throw std::invalid_argument("No shared chart portal");
            }
        }
        path.insert(path.end(), p.points.begin() + (i ? 1 : 0), p.points.end());
    }
    return path;
}
} // namespace Slic3r::FormorixGeometry
