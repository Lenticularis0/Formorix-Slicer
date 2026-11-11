#include "FormorixMaterial.hpp"
#include "FormorixMaterialInternal.hpp"
#include "FormorixGeometry.hpp"
#include "Point.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::FormorixMaterial {
namespace {
void check_cancel(const std::function<bool()>& cancel)
{
    if (cancel && cancel())
        throw detail::Failure(Status::Canceled, "Material volume audit canceled");
}
using Ring = std::array<Vec3d, 4>;
std::array<Vec3d, 2> height_centers(const Station& s)
{
    if (!s.center.allFinite() || !s.axis.allFinite() || s.axis.norm() < 1e-12 || !std::isfinite(s.width) || !std::isfinite(s.height) ||
        s.width <= 0. || s.height <= 0. || s.center.cwiseAbs().maxCoeff() > 1e7)
        throw detail::Failure(Status::InvalidInput, "Invalid material station or cross-section");
    const Vec3d axis   = s.axis.normalized();
    const Vec3d height = axis * (s.height / 2.);
    std::array<Vec3d, 2> centers{{s.center - height, s.center + height}};
    if (s.height_endpoints) {
        const auto& endpoints = *s.height_endpoints;
        if (!endpoints[0].allFinite() || !endpoints[1].allFinite())
            throw detail::Failure(Status::InvalidInput, "Nonfinite shared material endpoints");
        const double scale = std::max(
            {1., s.center.cwiseAbs().maxCoeff(), endpoints[0].cwiseAbs().maxCoeff(), endpoints[1].cwiseAbs().maxCoeff()});
        const double allowance = 64. * std::numeric_limits<double>::epsilon() * scale;
        if ((endpoints[1] - endpoints[0]).dot(axis) <= 0. || (endpoints[0] - centers[0]).norm() > allowance ||
            (endpoints[1] - centers[1]).norm() > allowance)
            throw detail::Failure(Status::InvalidInput, "Shared material height endpoints disagree with the station definition");
        centers = endpoints;
    }
    return centers;
}
Ring ring(const SweepRun& run, std::size_t i, const Options& options)
{
    const Station& s   = run.stations[i];
    const auto centers = height_centers(s);
    const Vec3d axis   = s.axis.normalized();
    auto side          = [&](const Vec3d& delta) -> Vec3d {
        if (!delta.allFinite() || delta.norm() < 1e-9)
            throw detail::Failure(Status::InvalidSweep, "Collapsed material segment");
        const Vec3d cross = axis.cross(delta.normalized());
        if (cross.norm() < 1e-8)
            throw detail::Failure(Status::InvalidSweep, "Material axis is parallel to its path");
        return cross.normalized();
    };
    Vec3d lateral;
    if (i == 0)
        lateral = side(run.stations[1].center - s.center);
    else if (i + 1 == run.stations.size())
        lateral = side(s.center - run.stations[i - 1].center);
    else {
        const Vec3d incoming = side(s.center - run.stations[i - 1].center);
        const Vec3d outgoing = side(run.stations[i + 1].center - s.center);
        const Vec3d sum      = incoming + outgoing;
        if (sum.norm() < 1e-8)
            throw detail::Failure(Status::InvalidSweep, "Reversing material turn has no finite miter");
        const double denominator = sum.dot(incoming);
        if (denominator <= 0. || sum.norm() / denominator > options.miter_limit)
            throw detail::Failure(Status::InvalidSweep, "Material turn exceeds the miter limit");
        lateral = sum / denominator;
    }
    lateral *= s.width / 2.;
    // Identical shared rings and diagonal choices on both incident cells.
    return {{centers[0] - lateral, centers[0] + lateral, centers[1] + lateral, centers[1] - lateral}};
}
FormorixGeometry::Mesh cell(const Ring& a, const Ring& b)
{
    FormorixGeometry::Mesh mesh;
    mesh.vertices.insert(mesh.vertices.end(), a.begin(), a.end());
    mesh.vertices.insert(mesh.vertices.end(), b.begin(), b.end());
    // Ring normal follows travel because lateral = axis cross tangent.
    mesh.faces = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}};
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        mesh.faces.emplace_back(i, j, j + 4);
        mesh.faces.emplace_back(i, j + 4, i + 4);
    }
    return mesh;
}
void append(FormorixGeometry::Mesh& to, const FormorixGeometry::Mesh& from)
{
    const int base = int(to.vertices.size());
    to.vertices.insert(to.vertices.end(), from.vertices.begin(), from.vertices.end());
    for (const auto& face : from.faces)
        to.faces.emplace_back(face[0] + base, face[1] + base, face[2] + base);
}
} // namespace

Result audit(const std::vector<Target>& targets,
             const FormorixGeometry::Mesh& core,
             const std::vector<SweepRun>& runs,
             const Options& options,
             const std::function<bool()>& cancel)
{
    Result out;
    try {
        if (targets.empty() || targets.size() > 32 || options.max_stations < 2 || options.max_sweep_faces < 12 ||
            options.max_boolean_faces < 12 || options.max_boolean_operations == 0 || options.max_distance_leaves == 0 ||
            options.max_distance_queries == 0 || !std::isfinite(options.distance_slack) || options.distance_slack <= 0. ||
            !std::isfinite(options.miter_limit) || options.miter_limit < 1.)
            throw detail::Failure(Status::InvalidInput, "Invalid material audit targets or resource limits");
        std::set<int> ids;
        for (const auto& target : targets)
            if (target.region <= 0 || target.material.empty() || target.mesh.faces.empty() || !ids.insert(target.region).second)
                throw detail::Failure(Status::InvalidInput, "Material targets require distinct regions and nonempty solids");
        std::size_t input_faces = 0, input_vertices = 0;
        auto count_input = [&](const FormorixGeometry::Mesh& mesh) {
            if (mesh.faces.size() > options.max_boolean_faces || mesh.vertices.size() > options.max_boolean_faces ||
                mesh.faces.size() > options.max_sweep_faces - input_faces ||
                mesh.vertices.size() > options.max_sweep_faces - input_vertices)
                throw detail::Failure(Status::ResourceLimit, "Aggregate material input geometry exceeds the budget");
            input_faces += mesh.faces.size();
            input_vertices += mesh.vertices.size();
        };
        count_input(core);
        for (const auto& target : targets) {
            count_input(target.mesh);
            count_input(target.outer_surface);
        }
        std::vector<detail::Cell> cells;
        std::size_t stations = 0;
        for (std::size_t r = 0; r < runs.size(); ++r) {
            check_cancel(cancel);
            const auto& run   = runs[r];
            const auto target = std::find_if(targets.begin(), targets.end(), [&](const Target& t) { return t.region == run.region; });
            if (run.head < 0 || run.head > 2 || run.layer < 0 || run.stations.size() < 2 ||
                run.supplied_volumes.size() + 1 != run.stations.size() || target == targets.end() || run.material != target->material)
                throw detail::Failure(Status::InvalidInput, "Material run has invalid ownership or segment supply");
            if (run.stations.size() > options.max_stations - std::min(stations, options.max_stations))
                throw detail::Failure(Status::ResourceLimit, "Material station budget exceeded");
            stations += run.stations.size();
            for (const auto& station : run.stations)
                (void) height_centers(station); // validate even stations removed by exact collinear reduction
            for (double supply : run.supplied_volumes)
                if (!std::isfinite(supply) || supply <= 0.)
                    throw detail::Failure(Status::InvalidInput, "Extruding material segment requires positive finite supply");
            SweepRun reduced;
            reduced.stations = detail::simplify_stations(run.stations);
            if (reduced.stations.size() - 1 > options.max_sweep_faces / 12 - std::min(cells.size(), options.max_sweep_faces / 12))
                throw detail::Failure(Status::ResourceLimit, "Material sweep face budget exceeded");
            Ring previous = ring(reduced, 0, options);
            for (std::size_t i = 1; i < reduced.stations.size(); ++i) {
                check_cancel(cancel);
                const Ring next = ring(reduced, i, options);
                auto mesh       = cell(previous, next);
                append(out.diagnostic_cells, mesh);
                cells.push_back({r, std::move(mesh)});
                previous = next;
            }
        }
        out.cells = cells.size();
        detail::audit_exact(out, targets, core, runs, cells, options, cancel);
        out.status = Status::Complete;
    } catch (const detail::Failure& e) {
        const auto status    = e.status;
        auto diagnostic      = std::move(out.diagnostic_cells);
        const auto cells     = out.cells;
        out                  = {};
        out.diagnostic_cells = std::move(diagnostic);
        out.cells            = cells;
        out.status           = status;
        out.error            = e.what();
    } catch (const std::exception& e) {
        auto diagnostic      = std::move(out.diagnostic_cells);
        out                  = {};
        out.diagnostic_cells = std::move(diagnostic);
        out.status           = Status::BooleanFailure;
        out.error            = e.what();
    }
    return out;
}
} // namespace Slic3r::FormorixMaterial
