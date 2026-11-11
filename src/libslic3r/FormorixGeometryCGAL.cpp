#include "FormorixGeometry.hpp"
#include "FormorixGeometryInternal.hpp"
#include "I18N.hpp"
#include "Point.hpp"
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/squared_distance_3.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Surface_mesh/Surface_mesh.h>
#include <CGAL/Kernel/global_functions_3.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/Polygon_mesh_processing/corefinement.h>
#include <CGAL/Polygon_mesh_processing/clip.h>
#include <CGAL/Polygon_mesh_processing/measure.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/Polygon_mesh_processing/self_intersections.h>
#include <CGAL/Polygon_mesh_processing/repair_degeneracies.h>
#include <CGAL/number_utils.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace Slic3r::FormorixGeometry {
namespace {
using K         = CGAL::Exact_predicates_exact_constructions_kernel;
using ExactMesh = CGAL::Surface_mesh<K::Point_3>;
namespace PMP   = CGAL::Polygon_mesh_processing;
bool valid(const ExactMesh& m)
{
    return !m.is_empty() && CGAL::is_triangle_mesh(m) && CGAL::is_closed(m) && !PMP::does_self_intersect(m) &&
           PMP::does_bound_a_volume(m) && PMP::volume(m) > 0;
}
ExactMesh exact(const Mesh& m)
{
    if (m.vertices.size() > 200000 || m.faces.size() > 200000)
        throw std::invalid_argument("Boolean resource limit");
    ExactMesh out;
    for (const Vec3d& p : m.vertices) {
        if (!p.allFinite() || p.cwiseAbs().maxCoeff() > 1e7)
            throw std::invalid_argument("Invalid coordinate");
        out.add_vertex(K::Point_3(p.x(), p.y(), p.z()));
    }
    for (const auto& f : m.faces) {
        for (int i : f)
            if (i < 0 || std::size_t(i) >= m.vertices.size())
                throw std::invalid_argument("Invalid face index");
        if (f[0] == f[1] || f[1] == f[2] || f[2] == f[0] ||
            CGAL::collinear(out.point(ExactMesh::Vertex_index(f[0])), out.point(ExactMesh::Vertex_index(f[1])),
                            out.point(ExactMesh::Vertex_index(f[2]))))
            throw std::invalid_argument("Degenerate face");
        if (out.add_face(ExactMesh::Vertex_index(f[0]), ExactMesh::Vertex_index(f[1]), ExactMesh::Vertex_index(f[2])) ==
            ExactMesh::null_face())
            throw std::invalid_argument("Non-manifold mesh");
    }
    return out;
}
Vec3d rounded(const K::Point_3& p, double& bound, bool grid)
{
    Vec3d q(CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
    // One shared binary grid for ALL solids and interfaces. Exact Boolean
    // intersections may differ by less than a double ULP; independent rounding
    // then creates sliver inversions. Record the actual displacement, including
    // this sub-nanometre quantisation, and reject any invalid converted topology.
    if (grid)
        for (int k = 0; k < 3; ++k)
            q[k] = std::ldexp(std::round(std::ldexp(q[k], 30)), -30);
    const auto squared = CGAL::squared_distance(p, K::Point_3(q.x(), q.y(), q.z()));
    if (squared != 0) {
        const auto interval = CGAL::to_interval(squared);
        bound = std::max(bound, std::nextafter(std::sqrt(std::max(0., interval.second)), INFINITY));
    }
    return q;
}
Mesh materialize(const ExactMesh& m, double& bound, bool grid)
{
    ExactMesh representable = m;
    std::set<std::tuple<double, double, double>> allowed;
    for (auto v : representable.vertices()) {
        const Vec3d p = rounded(representable.point(v), bound, grid);
        allowed.emplace(p.x(), p.y(), p.z());
        representable.point(v) = K::Point_3(p.x(), p.y(), p.z());
    }
    // Topological removal of zero-area faces keeps the boundary closed. Never
    // smooth the solid or permit repair to introduce a new coordinate.
    if (grid && !PMP::remove_degenerate_faces(representable))
        throw std::invalid_argument("Unrepresentable degenerate output");
    Mesh out;
    std::map<ExactMesh::Vertex_index, int> ids;
    for (auto v : representable.vertices()) {
        const Vec3d p = rounded(representable.point(v), bound, grid);
        if (!allowed.count(std::make_tuple(p.x(), p.y(), p.z())))
            throw std::invalid_argument("Repair changed vertex coordinates");
        ids[v] = int(out.vertices.size());
        out.vertices.push_back(p);
    }
    for (auto f : representable.faces()) {
        auto h = representable.halfedge(f);
        auto a = representable.source(h), b = representable.target(h), c = representable.target(representable.next(h));
        out.faces.emplace_back(ids.at(a), ids.at(b), ids.at(c));
    }
    return out;
}
struct PointLess
{
    bool operator()(const K::Point_3& a, const K::Point_3& b) const { return K().less_xyz_3_object()(a, b); }
};
using FaceKey = std::array<K::Point_3, 3>;
struct FaceLess
{
    bool operator()(const FaceKey& a, const FaceKey& b) const
    { return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), PointLess{}); }
};
FaceKey triangle(const ExactMesh& m, ExactMesh::Face_index f)
{
    auto h = m.halfedge(f);
    return {m.point(m.source(h)), m.point(m.target(h)), m.point(m.target(m.next(h)))};
}
} // namespace
bool valid_solid(const Mesh& m)
{
    try {
        return valid(exact(m));
    } catch (const std::exception&) {
        return false;
    }
}
double volume(const Mesh& m)
{
    const auto e = exact(m);
    if (!valid(e))
        throw std::invalid_argument("Invalid solid");
    return CGAL::to_double(PMP::volume(e));
}
static Partition partition_exact(const Mesh& solid, const std::vector<std::pair<int, ExactMesh>>& requests, bool clip)
{
    Partition out;
    try {
        if (requests.empty() || requests.size() > 32)
            throw std::invalid_argument("Invalid region count");
        ExactMesh remaining = exact(solid);
        if (!valid(remaining))
            throw std::invalid_argument("Source is not a valid oriented solid");
        const auto original_volume = PMP::volume(remaining);
        const ExactMesh original = remaining;
        out.input_volume           = CGAL::to_double(original_volume);
        std::vector<ExactMesh> solids;
        std::vector<int> regions;
        std::set<int> ids;
        for (const auto& request : requests) {
            if (!ids.insert(request.first).second)
                throw std::invalid_argument("Duplicate shell region");
            ExactMesh cutter = request.second;
            if (!valid(cutter))
                throw std::invalid_argument("Offset is self-intersecting or not a valid volume");
            if (clip) {
                ExactMesh source = original, clipped;
                if (!PMP::corefine_and_compute_intersection(source, cutter, clipped) || !valid(clipped))
                    throw std::invalid_argument("Clipped shell is not a valid volume");
                cutter = std::move(clipped);
            }
            const auto cutter_volume = PMP::volume(cutter);
            ExactMesh skin, core, outside;
            std::array<std::optional<ExactMesh*>, 4> outputs{};
            outputs[1]        = &skin;
            outputs[2]        = &core;
            outputs[3]        = &outside;
            const auto result = PMP::corefine_and_compute_boolean_operations(remaining, cutter, outputs);
            if (!result[1] || !result[2] || !result[3])
                throw std::invalid_argument("Non-manifold Boolean result");
            // Strict rejection prevents silent region priority, missing support walls,
            // overthick shells and overlapping material assignments.
            if (!outside.is_empty() && PMP::volume(outside) != 0)
                throw std::invalid_argument("Shell exceeds available wall or overlaps another region");
            if (!valid(skin) || (!core.is_empty() && !valid(core)) || PMP::volume(skin) != cutter_volume)
                throw std::invalid_argument("Invalid shell/core partition");
            solids.push_back(std::move(skin));
            regions.push_back(request.first);
            remaining = std::move(core);
        }
        if (!remaining.is_empty()) {
            solids.push_back(std::move(remaining));
            regions.push_back(0);
        }
        // Co-refine all final neighbours, so interfaces use the same exact triangles.
        for (std::size_t i = 0; i < solids.size(); ++i)
            for (std::size_t j = i + 1; j < solids.size(); ++j)
                PMP::corefine(solids[i], solids[j]);
        K::FT exact_sum = 0;
        for (const auto& part : solids) {
            if (!valid(part))
                throw std::invalid_argument("Invalid refined part");
            exact_sum += PMP::volume(part);
        }
        if (exact_sum != original_volume)
            throw std::invalid_argument("Exact volume conservation failed");
        auto convert = [&](bool grid) {
            Partition candidate;
            candidate.input_volume = out.input_volume;
            candidate.output_topology_preserved = !grid;
            std::map<FaceKey, std::pair<int, FaceKey>, FaceLess> owners;
            std::map<std::pair<int, int>, Mesh> interfaces;
            double materialized_sum = 0.;
            std::vector<ExactMesh> converted_parts;
            for (std::size_t i = 0; i < solids.size(); ++i) {
                for (auto f : solids[i].faces()) {
                    const auto oriented = triangle(solids[i], f);
                    auto key            = oriented;
                    std::sort(key.begin(), key.end(), PointLess{});
                    auto [it, added] = owners.emplace(key, std::make_pair(regions[i], oriented));
                    if (!added) {
                        const auto pair = std::minmax(it->second.first, regions[i]);
                        std::array<Vec3d, 3> points;
                        for (int k = 0; k < 3; ++k)
                            points[k] = rounded(oriented[k], candidate.rounding_bound, grid);
                        auto point = [](const Vec3d& p) { return K::Point_3(p.x(), p.y(), p.z()); };
                        if (CGAL::collinear(point(points[0]), point(points[1]), point(points[2])))
                            continue;
                        if (regions[i] != pair.first)
                            std::swap(points[1], points[2]);
                        Mesh& shared = interfaces[pair];
                        const int n  = int(shared.vertices.size());
                        for (const auto& p : points)
                            shared.vertices.push_back(p);
                        shared.faces.emplace_back(n, n + 1, n + 2);
                    }
                }
                Mesh rounded_mesh    = materialize(solids[i], candidate.rounding_bound, grid);
                const auto converted = exact(rounded_mesh);
                if (!CGAL::is_closed(converted))
                    throw std::invalid_argument("Double conversion opens solid");
                if (PMP::does_self_intersect(converted))
                    throw std::invalid_argument("Double conversion creates self-intersections");
                if (!valid(converted))
                    throw std::invalid_argument("Double conversion invalidates volume orientation");
                const double part_volume = CGAL::to_double(PMP::volume(converted));
                materialized_sum += part_volume;
                candidate.parts.push_back({regions[i], std::move(rounded_mesh), part_volume});
                converted_parts.push_back(converted);
            }
            // Rounding is not permission for two materials to occupy the same volume.
            for (std::size_t i = 0; i < converted_parts.size(); ++i)
                for (std::size_t j = i + 1; j < converted_parts.size(); ++j) {
                    ExactMesh a = converted_parts[i], b = converted_parts[j], overlap;
                    if (!PMP::corefine_and_compute_intersection(a, b, overlap) || (!overlap.is_empty() && PMP::volume(overlap) != 0))
                        throw std::invalid_argument("Double conversion overlaps material volumes");
                }
            candidate.volume_residual = materialized_sum - candidate.input_volume;
            for (auto& entry : interfaces)
                candidate.interfaces.push_back({entry.first.first, entry.first.second, std::move(entry.second)});
            if (solids.size() > 1 && candidate.interfaces.empty())
                throw std::invalid_argument("Shared interface was not recovered");
            return candidate;
        };
        // Prefer full double precision when every part and shared interface can
        // be represented consistently. The common grid remains a validated
        // fallback for exact intersections whose output would otherwise fold.
        try {
            out = convert(false);
        } catch (const std::invalid_argument&) {
            out = convert(true);
        }
    } catch (const std::exception& e) {
        out.parts.clear();
        out.interfaces.clear();
        out.output_topology_preserved = false;
        out.error = e.what();
    }
    return out;
}

Partition partition_meshes(const Mesh& solid, const std::vector<std::pair<int, Mesh>>& requests, bool clip)
{
    try {
        std::vector<std::pair<int, ExactMesh>> inputs;
        for (const auto& request : requests)
            inputs.emplace_back(request.first, exact(request.second));
        return partition_exact(solid, inputs, clip);
    } catch (const std::exception& e) {
        Partition out;
        out.error = e.what();
        return out;
    }
}

namespace {
using EdgeKey = std::array<K::Point_3, 2>;
struct EdgeLess
{
    bool operator()(const EdgeKey& a, const EdgeKey& b) const
    { return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), PointLess{}); }
};
K::Point_3 point(const Vec3d& p) { return {p.x(), p.y(), p.z()}; }
EdgeKey edge_key(const K::Point_3& a, const K::Point_3& b)
{ return PointLess{}(a, b) ? EdgeKey{a, b} : EdgeKey{b, a}; }
struct PlanarSheet
{
    K::Plane_3 outer, inner;
};
PlanarSheet planar_sheet(const ShellRequest& request, const Mesh& prism)
{
    const auto& surface = request.surface;
    const auto count = surface.vertices.size();
    if (surface.faces.empty() || request.depths.size() != count || request.outward_normals.size() != count ||
        prism.vertices.size() != 2 * count)
        throw std::invalid_argument(_u8L("Invalid planar miter source"));
    const auto& face = surface.faces.front();
    PlanarSheet sheet{{point(surface.vertices[face[0]]), point(surface.vertices[face[1]]), point(surface.vertices[face[2]])},
                      {point(prism.vertices[count + face[0]]), point(prism.vertices[count + face[1]]),
                       point(prism.vertices[count + face[2]])}};
    const auto& n = request.outward_normals.front();
    const K::Vector_3 normal(n.x(), n.y(), n.z());
    if (sheet.outer.is_degenerate() || sheet.inner.is_degenerate() ||
        CGAL::cross_product(sheet.outer.orthogonal_vector(), normal).squared_length() != 0 ||
        CGAL::cross_product(sheet.outer.orthogonal_vector(), sheet.inner.orthogonal_vector()).squared_length() != 0)
        throw std::invalid_argument(_u8L("Planar miter retriangulation requires parallel planar sheets and a perpendicular normal"));
    for (std::size_t i = 0; i < count; ++i)
        if (request.depths[i] != request.depths.front() || !(request.outward_normals[i].array() == n.array()).all() ||
            !sheet.outer.has_on(point(surface.vertices[i])) || !sheet.inner.has_on(point(prism.vertices[count + i])))
            throw std::invalid_argument(_u8L("Planar miter retriangulation requires an exactly planar region with constant normal and depth"));
    return sheet;
}
struct PlanarClip
{
    std::size_t first, second;
    K::Plane_3 plane; // first region keeps negative, second keeps positive
    std::set<EdgeKey, EdgeLess> edges;
};
void complete_boundary(const ShellRequest& request, const PlanarClip& cut)
{
    std::map<std::pair<int, int>, unsigned> counts;
    for (const auto& face : request.surface.faces)
        for (int k = 0; k < 3; ++k)
            ++counts[std::minmax(face[k], face[(k + 1) % 3])];
    std::set<EdgeKey, EdgeLess> boundary;
    for (const auto& item : counts)
        if (item.second == 1) {
            const auto a = point(request.surface.vertices[item.first.first]);
            const auto b = point(request.surface.vertices[item.first.second]);
            if (cut.plane.has_on(a) && cut.plane.has_on(b))
                boundary.insert(edge_key(a, b));
        }
    if (boundary != cut.edges)
        throw std::invalid_argument(_u8L("Planar miter clipping requires a complete matching shared boundary, not a partial edge"));
    // A plane must not silently act on a second disconnected boundary interval.
    std::map<K::Point_3, unsigned, PointLess> degree;
    for (const auto& edge : boundary) {
        ++degree[edge[0]];
        ++degree[edge[1]];
    }
    unsigned ends = 0;
    for (const auto& entry : degree) {
        ends += entry.second == 1;
        if (entry.second > 2)
            throw std::invalid_argument(_u8L("Planar miter boundary branches"));
    }
    if (ends != 2)
        throw std::invalid_argument(_u8L("Planar miter requires one straight shared boundary chain"));
    for (const auto& vertex : request.surface.vertices)
        if (cut.plane.has_on(point(vertex)) && degree.count(point(vertex)) == 0)
            throw std::invalid_argument(_u8L("Planar miter touches an unrelated source vertex"));
}
} // namespace

Partition detail::partition_planar_miters(const Mesh& solid,
                                         const std::vector<ShellRequest>& requests,
                                         const std::vector<std::pair<int, Mesh>>& prisms,
                                         const std::vector<PlanarMiterSeam>& seams)
{
    Partition out;
    try {
        if (requests.empty() || requests.size() != prisms.size() || seams.empty())
            throw std::invalid_argument(_u8L("A folded miter has no validated planar shared boundary"));
        std::vector<std::optional<PlanarSheet>> sheets(requests.size());
        std::vector<std::pair<int, ExactMesh>> cutters;
        for (const auto& prism : prisms) {
            cutters.emplace_back(prism.first, exact(prism.second));
            if (!valid(cutters.back().second))
                throw std::invalid_argument(_u8L("The original shell prism is invalid before planar miter clipping"));
        }
        std::map<std::pair<std::size_t, std::size_t>, std::size_t> group_ids;
        std::vector<PlanarClip> cuts;
        for (const auto& seam : seams) {
            if (seam.first >= requests.size() || seam.second >= requests.size() || seam.first == seam.second)
                throw std::invalid_argument(_u8L("Invalid planar miter region index"));
            for (const auto index : {seam.first, seam.second})
                if (!sheets[index]) sheets[index] = planar_sheet(requests[index], prisms[index].second);
            const auto& a = requests[seam.first];
            for (int index : seam.vertices)
                if (index < 0 || std::size_t(index) >= a.surface.vertices.size())
                    throw std::invalid_argument(_u8L("Invalid planar miter boundary index"));
            const auto p = point(a.surface.vertices[seam.vertices[0]]), q = point(a.surface.vertices[seam.vertices[1]]);
            const auto inner = point(seam.inner[0]);
            if (CGAL::collinear(p, q, inner))
                throw std::invalid_argument(_u8L("The planar miter plane is degenerate"));
            K::Plane_3 plane(p, q, inner);
            for (const auto& vertex : seam.inner)
                if (!plane.has_on(point(vertex)) || !sheets[seam.first]->inner.has_on(point(vertex)) ||
                    !sheets[seam.second]->inner.has_on(point(vertex)))
                    throw std::invalid_argument(_u8L("The common miter seam does not lie on both exact planar inner sheets"));
            bool positive = false, negative = false;
            for (const auto& vertex : a.surface.vertices) {
                positive |= plane.has_on_positive_side(point(vertex));
                negative |= plane.has_on_negative_side(point(vertex));
            }
            if (positive == negative)
                throw std::invalid_argument(_u8L("The planar miter would cut the original outer contour"));
            if (positive) plane = plane.opposite();
            bool other_positive = false;
            for (const auto& vertex : requests[seam.second].surface.vertices) {
                if (plane.has_on_negative_side(point(vertex)))
                    throw std::invalid_argument(_u8L("Adjacent planar regions are not on complementary sides of their miter"));
                other_positive |= plane.has_on_positive_side(point(vertex));
            }
            if (!other_positive)
                throw std::invalid_argument(_u8L("The planar miter does not separate adjacent outer regions"));
            const auto key = std::make_pair(seam.first, seam.second);
            const auto [group, added] = group_ids.emplace(key, cuts.size());
            if (added) cuts.push_back({seam.first, seam.second, plane, {}});
            else if (cuts[group->second].plane != plane)
                throw std::invalid_argument(_u8L("The shared planar boundary does not define one exact miter plane"));
            cuts[group->second].edges.insert(edge_key(p, q));
        }
        for (const auto& cut : cuts) {
            complete_boundary(requests[cut.first], cut);
            complete_boundary(requests[cut.second], cut);
            for (const auto index : {cut.first, cut.second}) {
                auto& cutter = cutters[index].second;
                const auto plane = index == cut.first ? cut.plane : cut.plane.opposite();
                if (!PMP::clip(cutter, plane, CGAL::parameters::clip_volume(true).do_not_triangulate_faces(false)) || !valid(cutter))
                    throw std::invalid_argument(_u8L("Planar miter clipping did not produce a closed valid shell"));
                if (cutter.number_of_vertices() > 200000 || cutter.number_of_faces() > 200000)
                    throw std::invalid_argument(_u8L("Planar miter retriangulation exceeds the geometry budget"));
            }
        }
        std::size_t retriangulated = 0;
        for (std::size_t i = 0; i < cutters.size(); ++i) {
            if (!sheets[i]) continue;
            ++retriangulated;
            std::set<FaceKey, FaceLess> faces;
            bool inner_face = false;
            for (const auto face : cutters[i].second.faces()) {
                auto key = triangle(cutters[i].second, face);
                inner_face |= sheets[i]->inner.has_on(key[0]) && sheets[i]->inner.has_on(key[1]) && sheets[i]->inner.has_on(key[2]);
                std::sort(key.begin(), key.end(), PointLess{});
                faces.insert(key);
            }
            if (!inner_face)
                throw std::invalid_argument(_u8L("The planar miter consumes the entire inner face; a different wall strategy is required"));
            for (const auto& face : requests[i].surface.faces) {
                const auto& vertices = requests[i].surface.vertices;
                FaceKey key{point(vertices[face[0]]), point(vertices[face[1]]), point(vertices[face[2]])};
                std::sort(key.begin(), key.end(), PointLess{});
                if (faces.count(key) == 0)
                    throw std::invalid_argument(_u8L("Planar miter clipping changed an original outer triangle"));
            }
        }
        out = partition_exact(solid, cutters, true);
        if (out.ok()) out.planar_miter_regions = retriangulated;
    } catch (const std::exception& e) {
        out = {};
        out.error = e.what();
    }
    return out;
}
} // namespace Slic3r::FormorixGeometry
