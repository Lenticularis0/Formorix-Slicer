#pragma once
#include "FormorixMaterial.hpp"
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Slic3r::FormorixMaterial::detail {
struct Failure : std::runtime_error
{
    Status status;
    Failure(Status value, const char* text) : std::runtime_error(text), status(value) {}
    Failure(Status value, const std::string& text) : std::runtime_error(text), status(value) {}
};
struct Cell
{
    std::size_t run = 0;
    FormorixGeometry::Mesh mesh;
};
std::vector<Station> simplify_stations(const std::vector<Station>&, bool closed, const std::function<bool()>&);
void audit_exact(Result&,
                 const std::vector<Target>&,
                 const FormorixGeometry::Mesh&,
                 const std::vector<SweepRun>&,
                 const std::vector<Cell>&,
                 const Options&,
                 const std::function<bool()>&);
} // namespace Slic3r::FormorixMaterial::detail
