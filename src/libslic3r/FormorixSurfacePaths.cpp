#include "FormorixSurfacePaths.hpp"
#include "FormorixSurfacePathsInternal.hpp"
#include "FormorixGeometry.hpp"
#include "Point.hpp"
#include "I18N.hpp"
#include <cmath>
#include <exception>
#include <functional>
#include <set>
#include <string>

namespace Slic3r::FormorixSurfacePaths {
Result trace(const Input& input, const Options& options, const std::function<bool()>& canceled)
{
    Result result;
    try {
        if (canceled && canceled())
            throw detail::Failure(Status::Canceled, _u8L("Surface contour generation canceled"));
        if (input.surface.faces.empty() || input.surface.vertices.empty() || input.vertex_normals.size() != input.surface.vertices.size())
            throw detail::Failure(Status::InvalidInput,
                                  _u8L("Surface contours require a nonempty mesh and a normal at every source vertex"));
        if (!options.plane_normal.allFinite() || !std::isfinite(options.plane_normal.norm()) || options.plane_normal.norm() < 1e-12 ||
            options.plane_offsets.empty() || options.max_faces == 0 || options.max_planes == 0 || options.max_knots < 2 ||
            options.max_carrier_candidates == 0 || !std::isfinite(options.max_segment_length) || options.max_segment_length <= 0. ||
            !std::isfinite(options.normal_offset_distance) || options.normal_offset_distance < 0. ||
            !std::isfinite(options.interpolation_tolerance) || options.interpolation_tolerance <= 0.)
            throw detail::Failure(Status::InvalidInput, _u8L("Invalid surface contour planes, interpolation tolerance or resource limits"));
        if (input.surface.faces.size() > options.max_faces || input.surface.vertices.size() > options.max_knots ||
            options.plane_offsets.size() > options.max_planes)
            throw detail::Failure(Status::ResourceLimit, _u8L("Surface contour input exceeds its mesh or plane budget"));
        for (const auto& normal : input.vertex_normals)
            if (!normal.allFinite() || !std::isfinite(normal.norm()) || normal.norm() < 1e-12 || normal.cwiseAbs().maxCoeff() > 1e6)
                throw detail::Failure(Status::InvalidInput, _u8L("Invalid surface normal field"));
        std::set<double> distinct_planes;
        for (double offset : options.plane_offsets) {
            if (!std::isfinite(offset) || std::abs(offset) > 1e7)
                throw detail::Failure(Status::InvalidInput, _u8L("Invalid surface contour plane offset"));
            if (!distinct_planes.insert(offset).second)
                throw detail::Failure(Status::InvalidInput, _u8L("Repeated contour plane offsets would duplicate the same material path"));
        }
        result = detail::trace_exact(input, options, canceled);
    } catch (const detail::Failure& e) {
        result        = {};
        result.status = e.status;
        result.reason = e.what();
    } catch (const std::exception& e) {
        result        = {};
        result.status = Status::Failure;
        result.reason = e.what();
    }
    return result;
}
} // namespace Slic3r::FormorixSurfacePaths
