#pragma once
#include "FormorixSurfacePaths.hpp"
#include <functional>
#include <stdexcept>
#include <string>
namespace Slic3r::FormorixSurfacePaths::detail {
struct Failure : std::runtime_error
{
    Status status;
    Failure(Status value, const std::string& message) : std::runtime_error(message), status(value) {}
};
Result trace_exact(const Input&, const Options&, const std::function<bool()>&);
} // namespace Slic3r::FormorixSurfacePaths::detail
