#pragma once
#include "FormorixGeometry.hpp"
#include "Point.hpp"
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace Slic3r::FormorixGeometry::detail {
struct PlanarMiterSeam
{
    std::size_t first = 0, second = 0; // request indices, not region IDs
    std::array<int, 2> vertices{};     // original edge in the first request
    std::array<Vec3d, 2> inner;        // common miter points, identical for both requests
};
// The unmodified valid prisms enter CGAL once. Clipping and the common partition
// retain exact coordinates until the existing final materialization checks.
Partition partition_planar_miters(const Mesh&,
                                  const std::vector<ShellRequest>&,
                                  const std::vector<std::pair<int, Mesh>>& prisms,
                                  const std::vector<PlanarMiterSeam>&);
} // namespace Slic3r::FormorixGeometry::detail
