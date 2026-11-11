#include "FormorixHybrid.hpp"
#include "FormorixDeposition.hpp"
#include "Formorix5D.hpp"
#include "FormorixGeometry.hpp"
#include "FormorixPreview.hpp"
#include "FormorixProcess.hpp"
#include "FormorixSurfaces.hpp"
#include "Point.hpp"
#include "Config.hpp"
#include "PrintConfig.hpp"
#include "AABBTreeIndirect.hpp"
#include "ClipperUtils.hpp"
#include "Polygon.hpp"
#include "Model.hpp"
#include "TriangleSelector.hpp"
#include "libslic3r.h"
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
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace Slic3r::FormorixHybrid {
namespace FG = FormorixGeometry;
namespace PV = FormorixPreview;
namespace PS = FormorixProcess;
namespace FS = FormorixSurfaces;
namespace {
constexpr double pi = 3.14159265358979323846;
const char* preview_issue(PV::Issue issue)
{
    switch (issue) {
    case PV::Issue::Empty: return "The selected surface is empty.";
    case PV::Issue::Disconnected: return "The selected surface contains disconnected islands.";
    case PV::Issue::NonDisk: return "The selected surface has holes or a branching boundary unsupported by the continuous planner.";
    case PV::Issue::NotHeightField: return "The surface folds over its local projection or has multiple intervals in a raster row.";
    case PV::Issue::ThinWall: return "The available wall is too thin for the requested layer.";
    case PV::Issue::InvalidOffset: return "The requested normal offset folds or self-intersects.";
    case PV::Issue::FullWallVariable: return "Full-wall mode requires a local termination strategy for variable wall thickness.";
    case PV::Issue::VerticalDisabled: return "Vertical surfaces require explicit permission.";
    case PV::Issue::TiltExceeded: return "The desired nozzle direction exceeds the configured tilt and deviation.";
    case PV::Issue::TooLarge: return "The preview exceeds the current mesh or path resource limit.";
    case PV::Issue::Canceled: return "Calculation was canceled.";
    case PV::Issue::StalePaint: return "Surface markings no longer match the mesh topology.";
    case PV::Issue::MultipleHeads: return "A single surface requires one explicitly assigned head.";
    case PV::Issue::ProfileMissing: return "A required nozzle or material profile is unavailable.";
    case PV::Issue::ReferenceEdge: return "The selected reference edge is no longer valid.";
    default: return "The selected mesh is invalid for continuous surface planning.";
    }
}
using Key = std::tuple<double, double, double>;
Key key(const Vec3d& p) { return {p.x(), p.y(), p.z()}; }
void canceled(const std::function<bool()>& cancel)
{
    if (cancel && cancel())
        throw std::runtime_error("Hybrid calculation canceled");
}
FG::Mesh compact(const FG::Mesh& input)
{
    FG::Mesh out;
    std::map<Key, int> ids;
    for (const auto& f : input.faces) {
        Vec3i32 face;
        for (int k = 0; k < 3; ++k) {
            const auto& p = input.vertices.at(f[k]);
            if (!p.allFinite())
                throw std::invalid_argument("Non-finite hybrid vertex");
            auto [it, added] = ids.emplace(key(p), int(out.vertices.size()));
            if (added)
                out.vertices.push_back(p);
            face[k] = it->second;
        }
        out.faces.push_back(face);
    }
    return out;
}
void append(FG::Mesh& target, const indexed_triangle_set& mesh, const Transform3d& transform)
{
    const int base = int(target.vertices.size());
    for (const auto& p : mesh.vertices)
        target.vertices.push_back(transform * p.cast<double>());
    for (auto f : mesh.indices) {
        if (transform.linear().determinant() < 0.)
            std::swap(f[1], f[2]);
        target.faces.emplace_back(f[0] + base, f[1] + base, f[2] + base);
    }
}
struct Surface
{
    const FG::Mesh& mesh;
    std::vector<Vec3d> normals;
    AABBTreeIndirect::Tree3d tree;
    Surface(const FG::Mesh& m, const std::map<Key, Vec3d>& shared)
        : mesh(m), tree(AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(m.vertices, m.faces))
    {
        for (const auto& p : m.vertices)
            normals.push_back(shared.at(key(p)).normalized());
    }
    std::pair<Vec3d, Vec3d> at(const Vec3d& p) const
    {
        size_t face = 0;
        Vec3d q;
        const double d = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(mesh.vertices, mesh.faces, tree, p, face, q);
        if (d < 0. || !std::isfinite(d))
            throw std::runtime_error("Cannot locate selected surface");
        const auto& f = mesh.faces[face];
        const Vec3d a = mesh.vertices[f[0]], u = mesh.vertices[f[1]] - a, v = mesh.vertices[f[2]] - a, n = u.cross(v);
        const double det = n.squaredNorm();
        if (det < 1e-28)
            throw std::runtime_error("Degenerate selected surface");
        const double b = (q - a).cross(v).dot(n) / det, c = u.cross(q - a).dot(n) / det;
        Vec3d normal = (1. - b - c) * normals[f[0]] + b * normals[f[1]] + c * normals[f[2]];
        if (normal.norm() < 1e-12)
            throw std::runtime_error("Ambiguous selected normal");
        return {q, normal.normalized()};
    }
};
class Wall
{
    const FG::Mesh& mesh;
    AABBTreeIndirect::Tree3d tree;
    void trace(size_t idx, const Vec3d& p, const Vec3d& dir, double& nearest, std::vector<double>* crossings = nullptr) const
    {
        const auto& node = tree.node(idx);
        double low = 0., high = nearest;
        for (int a = 0; a < 3; ++a) {
            if (std::abs(dir[a]) < 1e-15) {
                if (p[a] < node.bbox.min()[a] - 1e-9 || p[a] > node.bbox.max()[a] + 1e-9)
                    return;
            } else {
                double x = (node.bbox.min()[a] - 1e-9 - p[a]) / dir[a], y = (node.bbox.max()[a] + 1e-9 - p[a]) / dir[a];
                if (x > y)
                    std::swap(x, y);
                low  = std::max(low, x);
                high = std::min(high, y);
                if (low > high)
                    return;
            }
        }
        if (node.is_inner()) {
            trace(idx * 2 + 1, p, dir, nearest, crossings);
            trace(idx * 2 + 2, p, dir, nearest, crossings);
            return;
        }
        const auto& f = mesh.faces[node.idx];
        const Vec3d a = mesh.vertices[f[0]], b = mesh.vertices[f[1]] - a, c = mesh.vertices[f[2]] - a;
        const Vec3d v = dir.cross(c), delta = p - a, q = delta.cross(b);
        const double determinant = b.dot(v);
        if (std::abs(determinant) < 1e-15)
            return;
        const double u = delta.dot(v) / determinant, w = dir.dot(q) / determinant, d = c.dot(q) / determinant;
        if (u >= -1e-9 && w >= -1e-9 && u + w <= 1. + 1e-9 && d > 1e-7 && d < nearest) {
            if (crossings)
                crossings->push_back(d);
            else
                nearest = d;
        }
    }

public:
    explicit Wall(const FG::Mesh& m) : mesh(m), tree(AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(m.vertices, m.faces)) {}
    double thickness(const Vec3d& p, const Vec3d& n) const
    {
        double d = INFINITY;
        trace(0, p, -n, d);
        return d;
    }
    bool contains(const Vec3d& p) const
    {
        size_t face = 0;
        Vec3d closest;
        const double distance_squared = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(mesh.vertices, mesh.faces, tree, p, face,
                                                                                                   closest);
        if (distance_squared >= 0. && distance_squared <= 1e-14)
            return true;
        // Three oblique parity rays avoid a fixed axis hitting coplanar edges.
        // This detects invalid center points; it is not a proof of the swept
        // material envelope between the adaptively sampled points.
        int inside = 0;
        for (const Vec3d& direction : {Vec3d(1., .417, .193), Vec3d(.271, 1., .613), Vec3d(.397, .227, 1.)}) {
            double limit = INFINITY;
            std::vector<double> hits;
            trace(0, p, direction.normalized(), limit, &hits);
            std::sort(hits.begin(), hits.end());
            hits.erase(std::unique(hits.begin(), hits.end(), [](double a, double b) { return std::abs(a - b) < 1e-7; }), hits.end());
            inside += int(hits.size() % 2);
        }
        return inside >= 2;
    }
};
Vec3d orientation(const Vec3d& normal, const FS::Settings& s, double limit)
{
    if (!std::isfinite(limit) || limit <= 0. || limit > 180.)
        throw std::runtime_error("Invalid machine tilt limit");
    const double tilt = std::acos(std::clamp(normal.z(), -1., 1.)) * 180. / pi;
    if (tilt >= 89.9 && !s.allow_vertical)
        throw std::runtime_error("Vertical 5D surface requires explicit permission");
    const double deviation = s.allow_deviation ? s.max_deviation : 0.;
    if (tilt > limit + deviation + 1e-5)
        throw std::runtime_error("Requested surface exceeds nominal nozzle tilt");
    return tilt <= limit ? normal : PV::interpolate_axis(normal, Vec3d::UnitZ(), (tilt - limit) / tilt);
}
std::vector<double> layer_heights(const FS::Settings& s, double full_depth)
{
    std::vector<double> heights;
    double depth           = 0.;
    const double requested = s.shell_mode == FS::ShellMode::FullWall ? full_depth : s.shell_thickness;
    for (int i = 0;; ++i) {
        if (s.shell_mode == FS::ShellMode::Layers ? i >= s.layers : depth >= requested - 1e-9)
            break;
        if (i >= 200)
            throw std::runtime_error("Hybrid shell exceeds 200 layers");
        double h = i < s.fine_final_layers ? s.fine_layer_height : s.layer_height;
        if (s.shell_mode != FS::ShellMode::Layers)
            h = std::min(h, requested - depth);
        if (h <= 0. || !std::isfinite(h))
            throw std::runtime_error("Invalid hybrid layer height");
        heights.push_back(h);
        depth += h;
    }
    if (heights.empty())
        throw std::runtime_error("Empty 5D shell");
    return heights;
}
void sample_triangles(const FG::Mesh& mesh, const std::function<void(const Vec3d&)>& sample)
{
    // Interior/edge samples cover every source triangle, with extra resolution
    // for larger triangles. Exhaustive surface proof is a separate polygon audit.
    size_t count = 0;
    for (const auto& f : mesh.faces) {
        const Vec3d a = mesh.vertices[f[0]], b = mesh.vertices[f[1]], c = mesh.vertices[f[2]];
        const int n = std::clamp(int(std::ceil(std::max({(b - a).norm(), (c - a).norm(), (b - c).norm()}) / .5)), 2, 16);
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n - i; ++j) {
                if (++count > 100000)
                    throw std::runtime_error("Hybrid surface audit resource limit");
                sample(a + (b - a) * (double(i) / n) + (c - a) * (double(j) / n));
            }
    }
}
double polygon_area(const Polygons& polygons)
{
    double a = 0.;
    for (const auto& p : polygons)
        a += p.area();
    return std::abs(a);
}
class SegmentIndex
{
    struct Primitive
    {
        size_t id;
        Eigen::AlignedBox3d bounds;
        AABBTreeIndirect::Tree3d::VectorType center;
        size_t idx() const { return id; }
        const Eigen::AlignedBox3d& bbox() const { return bounds; }
        const AABBTreeIndirect::Tree3d::VectorType& centroid() const { return center; }
    };
    AABBTreeIndirect::Tree3d tree;
    std::vector<Primitive> entries;
    void visit(size_t index, const Vec3d& p, std::vector<size_t>& hits) const
    {
        const auto& n = tree.node(index);
        if (!n.bbox.contains(p))
            return;
        if (n.is_inner()) {
            visit(index * 2 + 1, p, hits);
            visit(index * 2 + 2, p, hits);
        } else
            hits.push_back(n.idx);
    }

public:
    void add(size_t id, const Vec3d& a, const Vec3d& b, double radius)
    {
        const Vec3d r = Vec3d::Constant(radius);
        entries.push_back({id, Eigen::AlignedBox3d(a.cwiseMin(b) - r, a.cwiseMax(b) + r), (a + b) * .5});
    }
    void build() { tree.build(std::move(entries)); }
    std::vector<size_t> at(const Vec3d& p) const
    {
        std::vector<size_t> hits;
        if (!tree.empty())
            visit(0, p, hits);
        return hits;
    }
};

void coverage(RegionResult& out,
              const std::vector<PV::Pose>& outer,
              const Surface& surface,
              const PS::Profile& profile,
              bool flat,
              std::vector<Vec3d>& markers,
              const std::function<bool()>& cancel)
{
    const Vec3d n = surface.normals.front();
    Vec3d u       = std::abs(n.x()) < .8 ? Vec3d::UnitX() : Vec3d::UnitY();
    u             = (u - n * u.dot(n)).normalized();
    const Vec3d v = n.cross(u);
    auto project  = [&](const Vec3d& p) { return Vec2d(p.dot(u), p.dot(v)); };
    auto polygon  = [&](const std::vector<Vec2d>& ps) {
        Polygon p;
        for (const auto& q : ps)
            p.points.emplace_back(scale_(q.x()), scale_(q.y()));
        p.make_counter_clockwise();
        return p;
    };
    Polygons selected, covered, physical;
    for (const auto& f : out.surface.faces)
        selected.push_back(
            polygon({project(out.surface.vertices[f[0]]), project(out.surface.vertices[f[1]]), project(out.surface.vertices[f[2]])}));
    const double scallop   = std::min(.004, profile.layer_height * profile.height_correction / 4.);
    const double safe_half = PS::planar_spacing(profile, scallop) / 2.;
    for (size_t i = 1; i < outer.size(); ++i) {
        Vec2d a = project(outer[i - 1].position), b = project(outer[i].position), delta = b - a;
        if (delta.norm() < 1e-10)
            continue;
        const Vec2d side(-delta.y() / delta.norm(), delta.x() / delta.norm());
        auto rectangle = [&](double radius) {
            return polygon({a - side * radius, b - side * radius, b + side * radius, a + side * radius});
        };
        covered.push_back(rectangle(safe_half));
        physical.push_back(rectangle(profile.line_width * profile.width_correction / 2.));
    }
    selected          = union_(selected);
    covered           = union_(covered);
    physical          = union_(physical);
    const double area = polygon_area(selected);
    if (area <= 0.)
        throw std::runtime_error("Unusable surface projection");
    out.coverage.uncovered_fraction = polygon_area(diff(selected, covered)) / area;
    out.coverage.outside_fraction   = polygon_area(diff(physical, selected)) / area;
    // Flat, orthogonal nozzles admit an analytical whole-projection certificate.
    // The .004-mm scallop leaves room for integer projection/conversion errors.
    out.coverage.bounded               = flat && out.coverage.uncovered_fraction < 1e-10;
    out.accuracy.whole_surface_covered = out.coverage.bounded && out.coverage.outside_fraction < 1e-10;
    if (out.coverage.bounded)
        out.accuracy.bounds[4] = scallop + 3e-6;
    SegmentIndex nearby;
    for (size_t i = 1; i < outer.size(); ++i)
        nearby.add(i, outer[i - 1].position, outer[i].position, std::max(profile.line_width, profile.layer_height) * 2.);
    nearby.build();
    double sum = 0., sum2 = 0.;
    sample_triangles(out.surface, [&](const Vec3d& p) {
        canceled(cancel);
        ++out.coverage.samples;
        double error = INFINITY;
        for (size_t i : nearby.at(p)) {
            const Vec3d d       = outer[i].position - outer[i - 1].position;
            const double length = d.norm();
            if (length < 1e-10)
                continue;
            const double t               = std::clamp((p - outer[i - 1].position).dot(d) / d.squaredNorm(), 0., 1.);
            const Vec3d q                = outer[i - 1].position + t * d;
            const auto [contact, normal] = surface.at(q);
            const Vec3d tangent          = (d - normal * d.dot(normal)).normalized();
            const auto bead              = PS::place_bead(contact, normal, tangent, outer[i].axis, profile);
            const double x               = (p - bead.center).dot(bead.width_axis);
            if (std::abs(x) > bead.half_width)
                continue;
            const Vec3d top = bead.center + bead.width_axis * x +
                              bead.height_axis * bead.half_height *
                                  std::sqrt(std::max(0., 1. - x * x / (bead.half_width * bead.half_width)));
            error           = std::min(error, (p - top).norm());
        }
        if (!std::isfinite(error)) {
            ++out.coverage.missing_samples;
            if (markers.size() < 10000)
                markers.push_back(p);
            return;
        }
        out.coverage.sampled_maximum = std::max(out.coverage.sampled_maximum, error);
        sum += error;
        sum2 += error * error;
        if (error > .01 && markers.size() < 10000)
            markers.push_back(p);
    });
    if (out.coverage.missing_samples || out.coverage.samples == 0) {
        out.coverage.sampled_maximum = out.coverage.sampled_mean = out.coverage.sampled_rms = INFINITY;
    } else {
        out.coverage.sampled_mean = sum / out.coverage.samples;
        out.coverage.sampled_rms  = std::sqrt(sum2 / out.coverage.samples);
    }
}
} // namespace

bool Accuracy::verified(double tolerance) const
{
    double sum = 0.;
    for (double b : bounds) {
        if (!std::isfinite(b) || b < 0.)
            return false;
        sum += b;
    }
    return whole_surface_covered && material_volume_plausible && std::isfinite(tolerance) && tolerance > 0. && sum <= tolerance;
}
FloatMesh to_float(const FG::Mesh& source)
{
    FloatMesh out;
    out.mesh.indices = source.faces;
    for (const auto& p : source.vertices) {
        if (!p.allFinite() || p.cwiseAbs().maxCoeff() > 1e7)
            throw std::invalid_argument("Invalid double to Orca conversion");
        const Vec3f q = p.cast<float>();
        out.mesh.vertices.push_back(q);
        out.displacement_bound = std::max(out.displacement_bound, (q.cast<double>() - p).norm());
    }
    return out;
}
Input extract(const ModelObject& object, int instance, const FS::Project& project, const std::array<PS::Profile, 3>& profiles)
{
    if (instance < 0 || instance >= int(object.instances.size()))
        throw std::invalid_argument("Invalid hybrid instance");
    Input input;
    const auto check_overrides = [](const ModelConfig& config) {
        for (const auto& option : config.keys()) {
            const auto& allowed = Formorix5D::option_keys();
            if (std::find(allowed.begin(), allowed.end(), option) != allowed.end())
                continue;
            // Orca assigns its first/default extruder even without an override;
            // Formorix uses the explicit per-region and core head bindings.
            const auto* extruder = config.get().option<ConfigOptionInt>("extruder");
            if (option == "extruder" && extruder && extruder->value >= 0 && extruder->value <= 1)
                continue;
            throw std::runtime_error("Hybrid preview does not yet preserve object or part override: " + option);
        }
    };
    check_overrides(object.config);
    if (!object.layer_config_ranges.empty() || !object.layer_height_profile.empty())
        throw std::runtime_error("Hybrid preview does not yet preserve layer-range overrides or variable layer-height profiles");
    for (const auto* volume : object.volumes) {
        if (!volume->is_model_part())
            throw std::runtime_error("Hybrid preview does not yet support negative volumes, modifiers or support modifiers");
        check_overrides(volume->config);
        if (!volume->supported_facets.empty() || !volume->seam_facets.empty() || !volume->mmu_segmentation_facets.empty() ||
            !volume->fuzzy_skin_facets.empty())
            throw std::runtime_error(
                "Hybrid preview does not yet remap conventional support, seam, material or fuzzy-skin painting onto the computed core");
        if (!FS::painting_matches(*volume))
            throw std::runtime_error("Surface markings no longer match model topology");
        append(input.solid, volume->mesh().its, object.instances[instance]->get_matrix() * volume->get_matrix());
    }
    input.solid = compact(input.solid);
    for (const auto& r : project.regions) {
        RegionInput region;
        region.id       = r.id;
        region.settings = r.settings;
        for (const auto* volume : object.volumes)
            if (volume->is_model_part()) {
                TriangleSelector selector(volume->mesh());
                selector.deserialize(volume->formorix_facets.get_data());
                append(region.surface, selector.get_facets_strict(EnforcerBlockerType(r.id)),
                       object.instances[instance]->get_matrix() * volume->get_matrix());
            }
        if (region.surface.faces.empty())
            continue;
        if (!r.settings.head_mask || (r.settings.head_mask & (r.settings.head_mask - 1)))
            throw std::runtime_error("A hybrid region requires one assigned head");
        for (region.head = 0; region.head < 3; ++region.head)
            if (r.settings.head_mask & (1u << region.head))
                break;
        if (region.head > 2 || !project.heads[region.head].enabled || !PS::valid(profiles[region.head]))
            throw std::runtime_error("A painted region has no enabled resolved head profile");
        region.process              = profiles[region.head];
        region.process.layer_height = r.settings.layer_height;
        if (r.settings.direction == FS::Direction::Edge) {
            try {
                const Vec3d direction            = PV::reference_direction(object, instance, r.settings);
                region.settings.direction        = FS::Direction::Custom;
                region.settings.custom_direction = {direction.x(), direction.y(), direction.z()};
            } catch (PV::Issue) {
                throw std::runtime_error("The selected reference edge no longer belongs to this model instance");
            }
        }
        region.surface = compact(region.surface);
        input.regions.push_back(std::move(region));
    }
    return input;
}

Result generate(const Input& input, const std::function<bool()>& cancel)
{
    Result out;
    out.original = input.solid;
    try {
        if (input.regions.empty() || input.regions.size() > 32 || !FG::valid_solid(input.solid))
            throw std::runtime_error("Hybrid calculation requires marked surfaces on a valid closed solid");
        std::map<Key, Vec3d> shared;
        for (const auto& r : input.regions) {
            FG::make_atlas(r.surface);
            if (!PS::valid(r.process))
                throw std::runtime_error("Invalid per-head hybrid process profile");
            if (std::any_of(r.process.shrink_compensation.begin(), r.process.shrink_compensation.end(),
                            [](double value) { return std::abs(value - 1.) > 1e-12; }))
                throw std::runtime_error("Hybrid preview requires neutral material shrink compensation until the core and skin share a "
                                         "common compensated geometry");
            for (const auto& f : r.surface.faces) {
                const Vec3d n = (r.surface.vertices[f[1]] - r.surface.vertices[f[0]])
                                    .cross(r.surface.vertices[f[2]] - r.surface.vertices[f[0]]);
                for (int k = 0; k < 3; ++k) {
                    auto [it, added] = shared.emplace(key(r.surface.vertices[f[k]]), Vec3d::Zero());
                    it->second += n;
                }
            }
        }
        for (const auto& item : shared)
            if (item.second.norm() < 1e-12)
                throw std::runtime_error("Ambiguous normal at joined regions");
        Wall wall(input.solid);
        std::vector<FG::ShellRequest> requests;
        std::vector<std::vector<double>> heights;
        std::size_t mesh_elements = 0, motion_elements = 0;
        for (const auto& r : input.regions) {
            canceled(cancel);
            Surface surface(r.surface, shared);
            double minimum = INFINITY, maximum = 0.;
            bool boundary_unknown = false;
            sample_triangles(r.surface, [&](const Vec3d& p) {
                const auto [q, n] = surface.at(p);
                const double d    = wall.thickness(q, n);
                if (!std::isfinite(d)) {
                    boundary_unknown = true;
                    return;
                }
                minimum = std::min(minimum, d);
                maximum = std::max(maximum, d);
            });
            if (!std::isfinite(minimum))
                throw std::runtime_error("No inward wall intersection below selected surface");
            if (boundary_unknown && r.settings.shell_mode == FS::ShellMode::FullWall)
                throw std::runtime_error("Full-wall thickness is undefined at the selected boundary");
            if (boundary_unknown)
                out.warnings.push_back("Region " + std::to_string(r.id) +
                                       ": offset is clipped at the original boundary; wall thickness there is not verified");
            if (r.settings.shell_mode == FS::ShellMode::FullWall && maximum - minimum > 1e-4)
                throw std::runtime_error("Variable full-wall thickness needs a local layer termination strategy");
            heights.push_back(layer_heights(r.settings, minimum));
            const std::size_t layer_elements = r.surface.vertices.size() + r.surface.faces.size();
            if (layer_elements > (2000000 - mesh_elements) / heights.back().size())
                throw std::runtime_error("Hybrid layer mesh budget exceeded; reduce the marked area, mesh detail or layer count");
            mesh_elements += layer_elements * heights.back().size();
            double depth = 0.;
            for (double h : heights.back())
                depth += h;
            if (depth > minimum + 1e-6)
                throw std::runtime_error("Requested 5D shell exceeds sampled available wall thickness");
            requests.push_back({r.id, r.surface, surface.normals, std::vector<double>(r.surface.vertices.size(), depth)});
        }
        out.partition = FG::partition_clipped(input.solid, requests);
        if (!out.partition.ok())
            throw std::runtime_error(out.partition.error);
        for (const auto& p : out.partition.parts)
            if (p.region == 0)
                out.core = p.mesh;
        const auto source = to_float(input.solid);
        out.preview.core  = to_float(out.core).mesh;
        double clock      = 0.;
        for (size_t index = 0; index < input.regions.size(); ++index) {
            canceled(cancel);
            const auto& r = input.regions[index];
            Surface surface(r.surface, shared);
            RegionResult rr;
            rr.id          = r.id;
            rr.head        = r.head;
            rr.surface     = r.surface;
            rr.process     = r.process;
            rr.settings    = r.settings;
            rr.shell_depth = requests[index].depths.front();
            for (const auto& p : out.partition.parts)
                if (p.region == r.id)
                    rr.shell_volume = p.volume;
            const auto selected   = to_float(r.surface);
            rr.accuracy.bounds[2] = std::max(source.displacement_bound, selected.displacement_bound);
            bool flat             = true;
            const Vec3d normal    = surface.normals.front();
            for (const auto& n : surface.normals)
                flat = flat && (n - normal).norm() < 1e-8;
            flat = flat && (orientation(normal, r.settings, r.max_tilt) - normal).norm() < 1e-8;
            if (r.cad_reference) {
                const auto imported = FG::measure(r.surface, *r.cad_reference);
                if (imported.bounded)
                    rr.accuracy.bounds[0] = imported.upper_bound;
                FG::Mesh inner = r.surface;
                for (size_t i = 0; i < inner.vertices.size(); ++i)
                    inner.vertices[i] -= rr.shell_depth * surface.normals[i];
                auto reference            = *r.cad_reference;
                const auto distance       = reference.signed_distance;
                const double depth        = rr.shell_depth;
                reference.signed_distance = [distance, depth](const Vec3d& p) { return distance(p) + depth; };
                const auto error          = FG::measure(inner, reference);
                if (error.bounded)
                    rr.accuracy.bounds[1] = error.upper_bound + out.partition.rounding_bound;
            }
            auto thickest_profile         = r.process;
            thickest_profile.layer_height = *std::max_element(heights[index].begin(), heights[index].end());
            rr.pitch = PS::planar_spacing(thickest_profile,
                                          std::min(.003, thickest_profile.layer_height * thickest_profile.height_correction / 4.));
            // Reduce the seed pitch for strong variation of local normal. This is
            // conservative sampling, not a curvature proof or calibrated flow change.
            double alignment = 1.;
            for (const auto& n : surface.normals)
                alignment = std::min(alignment, n.dot(normal));
            rr.pitch *= std::max(.25, alignment);
            FS::Settings outer_settings         = r.settings;
            outer_settings.shell_mode           = FS::ShellMode::Layers;
            outer_settings.layers               = 1;
            outer_settings.layer_height         = *std::min_element(heights[index].begin(), heights[index].end());
            outer_settings.fine_final_layers    = 0;
            outer_settings.alternate_directions = false;
            PV::Options options;
            options.head                 = r.head;
            options.region               = r.id;
            options.line_width           = rr.pitch;
            options.speed                = PS::effective_speed(r.process);
            options.max_tilt             = r.max_tilt;
            options.validate_wall        = false;
            options.longitudinal_spacing = .5;
            const auto seed              = PV::generate({selected.mesh, source.mesh, {}}, outer_settings, options, cancel);
            if (!seed.ok())
                throw std::runtime_error(preview_issue(seed.errors.front()));
            std::vector<PV::Pose> outer;
            for (const auto& pose : seed.layers.back().path) {
                const auto [q, n] = surface.at(pose.position);
                if (outer.empty() || (q - outer.back().position).norm() > 1e-9)
                    outer.push_back({q, orientation(n, r.settings, r.max_tilt)});
            }
            // Adaptive refinement on the compensated curve. Curved error bounds
            // remain unknown: midpoint refinement is not a supremum proof.
            std::vector<PV::Pose> refined;
            std::function<void(PV::Pose, PV::Pose, int)> refine = [&](PV::Pose a, PV::Pose b, int level) {
                const auto [q, n] = surface.at((a.position + b.position) * .5);
                PV::Pose middle{q, orientation(n, r.settings, r.max_tilt)};
                if (level < 8 && ((q - (a.position + b.position) * .5).norm() > .0005 || (a.axis - b.axis).norm() > .015 ||
                                  (b.position - a.position).norm() > .5)) {
                    refine(a, middle, level + 1);
                    refine(middle, b, level + 1);
                } else
                    refined.push_back(b);
                if (refined.size() > 150000)
                    throw std::runtime_error("Hybrid adaptive path limit");
            };
            refined.push_back(outer.front());
            for (size_t i = 1; i < outer.size(); ++i)
                refine(outer[i - 1], outer[i], 0);
            outer = std::move(refined);
            // The legacy seed only contains one layer. Bound the final expanded
            // result, including one possible inter-layer travel per layer.
            if (outer.size() > (500000 - motion_elements) / heights[index].size())
                throw std::runtime_error("Hybrid movement budget exceeded; reduce the marked area or layer count");
            motion_elements += outer.size() * heights[index].size();
            if (flat)
                rr.accuracy.bounds[3] = rr.accuracy.bounds[2] + 1e-9;
            auto outer_profile         = r.process;
            outer_profile.layer_height = heights[index].front();
            coverage(rr, outer, surface, outer_profile, flat, out.unverified, cancel);
            double depth = rr.shell_depth;
            for (size_t rev = heights[index].size(); rev > 0; --rev) {
                canceled(cancel);
                const double h = heights[index][rev - 1];
                depth -= h;
                auto profile         = r.process;
                profile.layer_height = h;
                PV::Layer layer;
                layer.depth = depth;
                auto mesh   = r.surface;
                for (size_t i = 0; i < mesh.vertices.size(); ++i)
                    mesh.vertices[i] -= depth * surface.normals[i];
                layer.surface = to_float(mesh).mesh;
                std::vector<PV::Pose> tcp;
                for (size_t i = 0; i < outer.size(); ++i) {
                    canceled(cancel);
                    const auto [q, n] = surface.at(outer[i].position);
                    Vec3d tangent     = outer[std::min(i + 1, outer.size() - 1)].position - outer[i ? i - 1 : 0].position;
                    tangent -= n * tangent.dot(n);
                    if (tangent.norm() < 1e-10)
                        tangent = n.unitOrthogonal();
                    const auto bead = PS::place_bead(q - depth * n, n, tangent.normalized(), outer[i].axis, profile);
                    if (!wall.contains(bead.center))
                        throw std::runtime_error(
                            "Compensated 5D material center leaves the original solid; this boundary needs local path termination");
                    if (rev == heights[index].size()) {
                        const double penetration = (bead.support(-n) - (q - rr.shell_depth * n)).dot(-n);
                        rr.nominal_core_overlap  = std::max(rr.nominal_core_overlap, penetration);
                        if (penetration > 1e-6 && out.unverified.size() < 10000)
                            out.unverified.push_back(bead.support(-n));
                    }
                    layer.path.push_back({bead.center, outer[i].axis});
                    tcp.push_back({bead.tcp, outer[i].axis});
                }
                if (r.settings.alternate_directions && rev % 2) {
                    std::reverse(layer.path.begin(), layer.path.end());
                    std::reverse(tcp.begin(), tcp.end());
                }
                const int global_layer = int(out.preview.layers.size());
                auto& track            = out.preview.timeline.heads[r.head];
                if (!track.empty()) {
                    const double distance = (track.back().to.position - tcp.front().position).norm();
                    if (distance > 1e-9) {
                        track.push_back({r.head, clock, clock + distance / 80., track.back().to, tcp.front(), PV::MotionKind::Travel, false,
                                         global_layer, r.id, profile});
                        clock = track.back().end;
                    }
                }
                for (size_t i = 1; i < tcp.size(); ++i) {
                    const double length = (tcp[i].position - tcp[i - 1].position).norm();
                    if (length < 1e-9)
                        throw std::runtime_error("Collapsed compensated 5D path segment");
                    const double end = clock + length / PS::effective_speed(profile);
                    track.push_back({r.head, clock, end, tcp[i - 1], tcp[i], PV::MotionKind::Extrude, true, global_layer, r.id, profile});
                    clock = end;
                    rr.deposited_volume += (layer.path[i].position - layer.path[i - 1].position).norm() * PS::section_area(profile) *
                                           profile.flow_ratio;
                }
                out.preview.layers.push_back(std::move(layer));
            }
            rr.volume_ratio                       = rr.shell_volume > 0. ? rr.deposited_volume / rr.shell_volume : INFINITY;
            const auto cell                       = PS::planar_coverage(r.process, rr.pitch);
            rr.accuracy.material_volume_plausible = rr.nominal_core_overlap <= 1e-6 && std::abs(rr.volume_ratio - 1.) <= .1 &&
                                                    std::abs(cell.excess_area) / cell.union_area <= .1;
            sample_triangles(r.surface, [&](const Vec3d& p) {
                auto [q, n] = surface.at(p);
                rr.interface_samples.push_back(q - rr.shell_depth * n);
                rr.interface_normals.push_back(n);
            });
            if (!rr.accuracy.material_volume_plausible)
                out.warnings.push_back("Region " + std::to_string(r.id) +
                                       ": nominal material volume is inconsistent; flow was not corrected");
            if (rr.nominal_core_overlap > 1e-6)
                out.warnings.push_back("Region " + std::to_string(r.id) + ": nominal bead crosses the local core interface by " +
                                       std::to_string(rr.nominal_core_overlap) + " mm; this material overlap is not resolved");
            if (!rr.accuracy.verified()) {
                out.warnings.push_back("Region " + std::to_string(r.id) + ": 0.01 mm whole-surface accuracy is not verified");
                if (out.unverified.size() < 10000)
                    out.unverified.push_back(r.surface.vertices.front());
            }
            out.regions.push_back(std::move(rr));
            const int base = int(out.preview.selected.vertices.size());
            out.preview.selected.vertices.insert(out.preview.selected.vertices.end(), selected.mesh.vertices.begin(),
                                                 selected.mesh.vertices.end());
            for (const auto& f : selected.mesh.indices)
                out.preview.selected.indices.emplace_back(f[0] + base, f[1] + base, f[2] + base);
            out.preview.shell_depth = std::max(out.preview.shell_depth, requests[index].depths.front());
        }
        for (int h = 0; h < 3; ++h)
            out.preview.timeline.idle[h] = {input.solid.vertices.front() + Vec3d(-5. - 5. * h, 0., 5.), Vec3d::UnitZ()};
        if (!out.preview.timeline.valid())
            throw std::runtime_error("Invalid 5D-only preview timing");
    } catch (const std::exception& e) {
        out.error   = e.what();
        out.preview = {};
        out.regions.clear();
        out.core = {};
        out.partition.parts.clear();
        out.partition.interfaces.clear();
    }
    return out;
}

void audit_underlay(Result& result, const std::vector<UnderlaySegment>& paths, const std::function<bool()>& cancel)
{
    result.missing_underlay.clear();
    SegmentIndex nearby;
    for (size_t i = 0; i < paths.size(); ++i) {
        const auto& p = paths[i];
        if (std::isfinite(p.width) && std::isfinite(p.height) && p.width > 0. && p.height > 0. && p.from.allFinite() && p.to.allFinite())
            nearby.add(i, p.from - Vec3d(0., 0., p.height / 2.), p.to - Vec3d(0., 0., p.height / 2.),
                       std::max(p.width, p.height) / 2. + .05);
    }
    nearby.build();
    for (auto& region : result.regions) {
        region.underlay = {};
        if (region.interface_samples.size() != region.interface_normals.size())
            throw std::invalid_argument("Inconsistent interface audit data");
        for (size_t i = 0; i < region.interface_samples.size(); ++i) {
            canceled(cancel);
            const Vec3d q = region.interface_samples[i], direction = -region.interface_normals[i];
            double nearest = INFINITY;
            for (size_t path : nearby.at(q)) {
                const auto& p = paths[path];
                if (p.infill && !region.settings.use_infill)
                    continue;
                if (p.width <= 0. || p.height <= 0. || !p.from.allFinite() || !p.to.allFinite())
                    continue;
                Vec3d tangent       = p.to - p.from;
                tangent.z()         = 0.;
                const double length = tangent.norm();
                if (length < 1e-9)
                    continue;
                tangent /= length;
                const Vec3d side = Vec3d::UnitZ().cross(tangent), origin = p.from - Vec3d(0., 0., p.height / 2.);
                const Vec3d delta = q - origin;
                const double x = delta.dot(side) / (p.width / 2.), z = delta.z() / (p.height / 2.);
                const double dx = direction.dot(side) / (p.width / 2.), dz = direction.z() / (p.height / 2.);
                const double a = dx * dx + dz * dz, b = 2. * (x * dx + z * dz), c = x * x + z * z - 1.;
                double distance = 0.;
                if (c > 0.) {
                    const double discriminant = b * b - 4. * a * c;
                    if (a < 1e-15 || discriminant < 0.)
                        continue;
                    distance = (-b - std::sqrt(discriminant)) / (2. * a);
                    if (distance < 0.)
                        continue;
                }
                const double along = (delta + distance * direction).dot(tangent);
                if (along >= 0. && along <= length)
                    nearest = std::min(nearest, distance);
            }
            ++region.underlay.samples;
            region.underlay.maximum_gap = std::max(region.underlay.maximum_gap, nearest);
            if (nearest > std::min(.05, region.process.layer_height * .25)) {
                ++region.underlay.missing;
                if (result.missing_underlay.size() < 10000)
                    result.missing_underlay.push_back(q);
            }
        }
        result.warnings.push_back("Region " + std::to_string(region.id) +
                                  ": underlay is sampled, not a full contact or material-adhesion certificate");
        const std::string prefix = "Region " + std::to_string(region.id) + ": ";
        if (region.settings.dense_underlay)
            result.warnings.push_back(
                prefix + "requested dense transition underlay is not generated; only actual conventional Orca paths were audited");
        if (region.settings.additional_support)
            result.warnings.push_back(
                prefix + "additional 5D support is not generated; only existing conventional Orca support paths can contribute contact");
        if (region.settings.shell_mode == FS::ShellMode::FullWall)
            result.warnings.push_back(prefix +
                                      "full-wall 5D requires a prior supporting structure; completion and strength remain unverified");
        if (!region.settings.inherit_infill_first)
            result.warnings.push_back(prefix +
                                      "hybrid sequencing is not implemented; the core retains the Orca process infill-first setting");
        switch (region.settings.support_material) {
        case FS::SupportMaterial::Breakaway:
            result.warnings.push_back(prefix + "breakaway support suitability and interface adhesion are unverified");
            break;
        case FS::SupportMaterial::Soluble:
            result.warnings.push_back(prefix + "soluble support material suitability and interface adhesion are unverified");
            break;
        default:
            result.warnings.push_back(prefix + "automatic support-material selection and interface compatibility are unverified");
            break;
        }
    }
}
} // namespace Slic3r::FormorixHybrid
