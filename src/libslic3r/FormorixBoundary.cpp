#include "FormorixBoundary.hpp"
#include "FormorixDeposition.hpp"
#include "FormorixProcess.hpp"
#include "Point.hpp"
#include "I18N.hpp"
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
#include <utility>
#include <vector>

namespace Slic3r::FormorixBoundary {
namespace {
namespace PS = FormorixProcess;
struct Failure : std::runtime_error
{
    Status status;
    Failure(Status value, const std::string& message) : std::runtime_error(message), status(value) {}
};
void check_cancel(const std::function<bool()>& canceled)
{
    if (canceled && canceled())
        throw Failure(Status::Canceled, _u8L("Planar boundary layout canceled"));
}
struct Edge
{
    std::size_t from = 0, to = 0, face = 0, other = 0;
    unsigned count = 0;
};
Rectangle rectangle(const Domain& domain, const Options& options, double& deviation, const std::function<bool()>& canceled)
{
    if (domain.triangles.empty() || domain.vertices.empty())
        throw Failure(Status::InvalidInput, _u8L("A boundary layout requires a nonempty triangulated surface"));
    if (domain.triangles.size() > options.max_triangles || domain.vertices.size() > 3 * options.max_triangles)
        throw Failure(Status::ResourceLimit, _u8L("Planar boundary input exceeds the triangle budget"));
    std::map<std::pair<double, double>, std::size_t> unique;
    std::vector<Vec2d> vertices;
    std::vector<std::size_t> indices;
    for (const auto& p : domain.vertices) {
        check_cancel(canceled);
        if (!p.allFinite() || p.cwiseAbs().maxCoeff() > 1e7)
            throw Failure(Status::InvalidInput, _u8L("Invalid planar boundary coordinate"));
        const auto entry = unique.emplace(std::make_pair(p.x(), p.y()), vertices.size());
        if (entry.second)
            vertices.push_back(p);
        indices.push_back(entry.first->second);
    }
    std::map<std::pair<std::size_t, std::size_t>, Edge> edges;
    std::vector<std::vector<std::size_t>> neighbors(domain.triangles.size());
    std::set<std::size_t> used;
    int orientation = 0;
    for (std::size_t f = 0; f < domain.triangles.size(); ++f) {
        check_cancel(canceled);
        std::array<std::size_t, 3> ids;
        for (int k = 0; k < 3; ++k) {
            const int i = domain.triangles[f][k];
            if (i < 0 || std::size_t(i) >= indices.size())
                throw Failure(Status::InvalidInput, _u8L("Invalid planar boundary triangle index"));
            ids[k] = indices[i];
            used.insert(ids[k]);
        }
        const auto a = vertices[ids[1]] - vertices[ids[0]], b = vertices[ids[2]] - vertices[ids[0]];
        const double left = a.x() * b.y(), right = a.y() * b.x(), det = left - right;
        const double uncertainty = 16. * std::numeric_limits<double>::epsilon() * (std::abs(left) + std::abs(right));
        if (!std::isfinite(det) || std::abs(det) <= uncertainty)
            throw Failure(Status::Unsupported, _u8L("A degenerate or numerically unresolved projected triangle prevents boundary layout"));
        const int sign = det > 0. ? 1 : -1;
        if (orientation && orientation != sign)
            throw Failure(Status::Unsupported, _u8L("Folded or inconsistently oriented surface projection is unsupported"));
        orientation = sign;
        for (int k = 0; k < 3; ++k) {
            const auto from = ids[k], to = ids[(k + 1) % 3];
            const auto key = std::minmax(from, to);
            auto& edge     = edges[{key.first, key.second}];
            if (edge.count == 0)
                edge = {from, to, f, f, 1};
            else {
                if (edge.count != 1 || edge.from != to || edge.to != from)
                    throw Failure(Status::Unsupported, _u8L("The projected surface has a duplicate or nonmanifold edge"));
                edge.other = f;
                edge.count = 2;
                neighbors[f].push_back(edge.face);
                neighbors[edge.face].push_back(f);
            }
        }
    }
    std::vector<bool> visited(domain.triangles.size(), false);
    std::vector<std::size_t> queue{0};
    visited[0] = true;
    for (std::size_t i = 0; i < queue.size(); ++i)
        for (auto next : neighbors[queue[i]])
            if (!visited[next]) {
                visited[next] = true;
                queue.push_back(next);
            }
    if (queue.size() != domain.triangles.size())
        throw Failure(Status::Unsupported, _u8L("Disconnected projected regions require separate boundary layouts"));
    std::map<std::size_t, std::size_t> next;
    std::set<std::size_t> destinations;
    for (const auto& entry : edges) {
        const auto& edge = entry.second;
        if (edge.count == 1) {
            const auto a = orientation > 0 ? edge.from : edge.to, b = orientation > 0 ? edge.to : edge.from;
            if (!next.emplace(a, b).second || !destinations.insert(b).second)
                throw Failure(Status::Unsupported, _u8L("The projected boundary has a branch or a point contact"));
        }
    }
    if (next.size() < 4 || used.size() + domain.triangles.size() != edges.size() + 1)
        throw Failure(Status::Unsupported, _u8L("Holes or non-disk projected surfaces are not supported by rectangular boundary layout"));
    std::vector<std::size_t> loop;
    auto current     = next.begin()->first;
    const auto first = current;
    do {
        check_cancel(canceled);
        loop.push_back(current);
        const auto found = next.find(current);
        if (found == next.end() || loop.size() > next.size())
            throw Failure(Status::Unsupported, _u8L("The projected boundary is not a closed simple contour"));
        current = found->second;
    } while (current != first);
    if (loop.size() != next.size())
        throw Failure(Status::Unsupported, _u8L("Holes or multiple boundary contours are unsupported"));
    Rectangle rect{vertices[loop.front()], vertices[loop.front()]};
    for (auto i : loop) {
        rect.minimum = rect.minimum.cwiseMin(vertices[i]);
        rect.maximum = rect.maximum.cwiseMax(vertices[i]);
    }
    if ((rect.maximum - rect.minimum).minCoeff() <= 0.)
        throw Failure(Status::Unsupported, _u8L("The projected rectangle has no positive area"));
    std::array<bool, 4> sides{};
    double edge_error = 0.;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const auto& a = vertices[loop[i]];
        const auto& b = vertices[loop[(i + 1) % loop.size()]];
        const std::array<double, 4> errors{{std::max(std::abs(a.y() - rect.minimum.y()), std::abs(b.y() - rect.minimum.y())),
                                            std::max(std::abs(a.x() - rect.maximum.x()), std::abs(b.x() - rect.maximum.x())),
                                            std::max(std::abs(a.y() - rect.maximum.y()), std::abs(b.y() - rect.maximum.y())),
                                            std::max(std::abs(a.x() - rect.minimum.x()), std::abs(b.x() - rect.minimum.x()))}};
        const auto side = std::size_t(std::min_element(errors.begin(), errors.end()) - errors.begin());
        const std::array<double, 4> forward{{b.x() - a.x(), b.y() - a.y(), a.x() - b.x(), a.y() - b.y()}};
        if (errors[side] > options.rectangle_tolerance || forward[side] <= 0.)
            throw Failure(Status::Unsupported, _u8L("Sloping, curved or reentrant boundaries require a different finite-cap layout"));
        sides[side] = true;
        edge_error  = std::max(edge_error, errors[side]);
    }
    if (!std::all_of(sides.begin(), sides.end(), [](bool value) { return value; }))
        throw Failure(Status::Unsupported, _u8L("The projected contour does not cover all four rectangle sides"));
    // Every boundary segment lies within this strip of its assigned rectangle
    // side; monotonic traversal covers the side, including its corner approach.
    deviation = edge_error == 0. ? 0. : std::nextafter(2. * edge_error, INFINITY);
    return rect;
}
double arithmetic_allowance(double scale) { return 128. * std::numeric_limits<double>::epsilon() * std::max(1., scale); }
double displacement_upper(const Vec2d& a, const Vec2d& b)
{
    if ((a.array() == b.array()).all())
        return 0.;
    // Outward-rounded L1 also bounds Euclidean displacement.
    const double x = std::nextafter(std::abs(a.x() - b.x()), INFINITY);
    const double y = std::nextafter(std::abs(a.y() - b.y()), INFINITY);
    return std::nextafter(x + y, INFINITY);
}
} // namespace

Result plan(const Domain& domain, const PS::Profile& profile, const Options& options, const std::function<bool()>& canceled)
{
    Result out;
    try {
        if (options.max_triangles == 0 || options.max_triangles > 1000000 || options.max_rows == 0 || options.max_rows > 1000000 ||
            options.max_points < 2 || !std::isfinite(options.rectangle_tolerance) || options.rectangle_tolerance < 0.)
            throw Failure(Status::InvalidInput, _u8L("Invalid planar boundary resource limits or rectangle tolerance"));
        check_cancel(canceled);
        out.target             = rectangle(domain, options, out.boundary_deviation_bound, canceled);
        out.covered            = out.target;
        out.local_profile      = profile;
        const auto original    = PS::ribbon_section(profile);
        const double width     = out.target.maximum.x() - out.target.minimum.x();
        const double depth     = out.target.maximum.y() - out.target.minimum.y();
        const double allowance = arithmetic_allowance(std::max(width, depth));
        double rows            = std::round(depth / original.compacted_width);
        if (profile.local_width_limits) {
            const auto limits    = *profile.local_width_limits;
            const double minimum = std::max(limits.minimum_mm, original.height / profile.width_correction);
            if (minimum > limits.maximum_mm)
                throw Failure(Status::Unsupported, _u8L("Local width limits do not allow a valid rounded-rectangle cross-section"));
            const double pmin = PS::ribbon_section(profile, minimum).compacted_width;
            const double pmax = PS::ribbon_section(profile, limits.maximum_mm).compacted_width;
            const double low  = std::max(1., std::ceil((depth - allowance) / pmax));
            const double high = std::floor((depth + allowance) / pmin);
            if (high < low)
                throw Failure(Status::Unsupported,
                              _u8L("No complete ribbon count fits the rectangle within the explicit local width limits"));
            rows = std::clamp(rows, low, high);
            if (rows > double(options.max_rows))
                throw Failure(Status::ResourceLimit, _u8L("Planar boundary row budget exceeded"));
            const double pitch = depth / rows;
            const double pi    = std::acos(-1.);
            double requested = (pitch / PS::effective_surface_flow(profile) + original.height * (1. - pi / 4.)) / profile.width_correction;
            const double limit_allowance = arithmetic_allowance(std::max(std::abs(requested), limits.maximum_mm));
            if (requested < minimum - limit_allowance || requested > limits.maximum_mm + limit_allowance)
                throw Failure(Status::Unsupported, _u8L("The computed local width lies outside its explicit bounds"));
            requested   = std::clamp(requested, minimum, limits.maximum_mm);
            out.section = PS::ribbon_section(profile, requested);
            if (std::abs(depth - rows * out.section.compacted_width) > allowance)
                throw Failure(Status::Unsupported, _u8L("The requested local width cannot reproduce the selected ribbon spacing"));
            out.local_profile.line_width = requested;
            out.width_adjusted           = requested != profile.line_width;
            out.full_candidate           = true;
        } else {
            out.section = original;
            if (rows >= 1. && std::abs(depth - rows * original.compacted_width) <= allowance)
                out.full_candidate = true;
            else
                rows = std::floor(depth / original.compacted_width);
            if (rows < 1.)
                throw Failure(Status::Unsupported,
                              _u8L("The region is narrower than one fixed-width ribbon; explicit local width limits are required"));
            if (rows > double(options.max_rows))
                throw Failure(Status::ResourceLimit, _u8L("Planar boundary row budget exceeded"));
            if (!out.full_candidate) {
                const double occupied = rows * original.compacted_width;
                out.residual_width    = depth - occupied;
                out.covered.minimum.y() += out.residual_width / 2.;
                out.covered.maximum.y() = out.covered.minimum.y() + occupied;
                if (out.covered.minimum.y() > out.target.minimum.y())
                    out.remainder.push_back({out.target.minimum, {out.target.maximum.x(), out.covered.minimum.y()}});
                if (out.covered.maximum.y() < out.target.maximum.y())
                    out.remainder.push_back({{out.target.minimum.x(), out.covered.maximum.y()}, out.target.maximum});
            }
        }
        if (rows < 1. || !std::isfinite(rows))
            throw Failure(Status::Unsupported, _u8L("No finite ribbon layout fits this rectangle"));
        out.rows = std::size_t(rows);
        if (out.rows > (options.max_points - 2) / 2 + 1)
            throw Failure(Status::ResourceLimit, _u8L("Planar boundary station budget exceeded"));
        if (out.rows > 1 && width <= out.section.compacted_width)
            throw Failure(Status::Unsupported,
                          _u8L("The rectangle is too short for finite mitered turns in the requested raster direction"));
        out.speed = PS::ribbon_speed(out.local_profile);
        std::vector<double> borders(out.rows + 1);
        borders.front() = out.covered.minimum.y();
        borders.back()  = out.covered.maximum.y();
        for (std::size_t i = 1; i < out.rows; ++i)
            borders[i] = borders.front() + (borders.back() - borders.front()) * double(i) / double(out.rows);
        const double low = out.target.minimum.x(), high = out.target.maximum.x(), pitch = out.section.compacted_width;
        const double inner_left = low + pitch, inner_right = high - pitch;
        auto add = [&](const Vec2d& p, const Vec2d& minus, const Vec2d& plus) {
            if (!out.path.empty())
                out.supplied_volume += (p - out.path.back()).norm() * out.section.supplied_area;
            out.path.push_back(p);
            out.lateral_endpoints.push_back({{minus, plus}});
        };
        add({low, (borders[0] + borders[1]) / 2.}, {low, borders[0]}, {low, borders[1]});
        for (std::size_t row = 0; row < out.rows; ++row) {
            check_cancel(canceled);
            const bool right = row % 2 == 0;
            const double mid = (borders[row] + borders[row + 1]) / 2.;
            if (row + 1 == out.rows) {
                const double end = right ? high : low;
                add({end, mid}, {end, borders[right ? row : row + 1]}, {end, borders[right ? row + 1 : row]});
            } else {
                const double turn     = right ? high - pitch / 2. : low + pitch / 2.;
                const double next_mid = (borders[row + 1] + borders[row + 2]) / 2.;
                if (right) {
                    add({turn, mid}, {high, borders[row]}, {inner_right, borders[row + 1]});
                    add({turn, next_mid}, {high, borders[row + 2]}, {inner_right, borders[row + 1]});
                } else {
                    add({turn, mid}, {inner_left, borders[row + 1]}, {low, borders[row]});
                    add({turn, next_mid}, {inner_left, borders[row + 1]}, {low, borders[row + 2]});
                }
            }
        }
        for (std::size_t i = 0; i < out.path.size(); ++i) {
            check_cancel(canceled);
            auto side = [&](const Vec2d& d) -> Vec2d { return Vec2d(-d.y(), d.x()).normalized(); };
            Vec2d lateral;
            if (i == 0)
                lateral = side(out.path[1] - out.path[0]);
            else if (i + 1 == out.path.size())
                lateral = side(out.path[i] - out.path[i - 1]);
            else {
                const Vec2d before = side(out.path[i] - out.path[i - 1]), after = side(out.path[i + 1] - out.path[i]);
                const Vec2d sum = before + after;
                lateral         = sum / sum.dot(before);
            }
            lateral *= pitch / 2.;
            out.construction_bound = std::max({out.construction_bound,
                                               displacement_upper(out.path[i] - lateral, out.lateral_endpoints[i][0]),
                                               displacement_upper(out.path[i] + lateral, out.lateral_endpoints[i][1])});
        }
        const double coordinate_scale = std::max(out.target.minimum.cwiseAbs().maxCoeff(), out.target.maximum.cwiseAbs().maxCoeff());
        if (!std::isfinite(out.supplied_volume) || !std::isfinite(out.construction_bound) ||
            out.construction_bound > arithmetic_allowance(coordinate_scale))
            throw Failure(Status::Unsupported,
                          _u8L("Canonical boundary rails exceed the nominal profile's numerical construction allowance"));
        if (out.construction_bound > 0.)
            out.construction_bound = std::nextafter(out.construction_bound, INFINITY);
        check_cancel(canceled);
        out.status = Status::Ready;
        out.reason = out.full_candidate ? _u8L("Rectangular candidate requires the full spatial material audit") :
                                          _u8L("Fixed profile width leaves explicit unfilled boundary strips; Flow remains unchanged");
    } catch (const Failure& e) {
        out.status = e.status;
        out.reason = e.what();
        out.path.clear();
        out.lateral_endpoints.clear();
        out.full_candidate = false;
    } catch (const std::exception& e) {
        out.status = Status::InvalidInput;
        out.reason = e.what();
        out.path.clear();
        out.lateral_endpoints.clear();
        out.full_candidate = false;
    }
    return out;
}
} // namespace Slic3r::FormorixBoundary
