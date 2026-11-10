// Formorix Slicer additions, licensed under AGPL-3.0-or-later.
#pragma once

#include "Config.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Slic3r {
namespace Formorix5D {

constexpr std::size_t head_count = 3;
constexpr int schema_version = 1;

struct Axis {
    double position;
    double minimum;
    double maximum;
};

struct Head {
    // Machine X/Y and local head Z in mm; A/B are independent angles in degrees.
    // No rotation order, inverse kinematics or machine commands are implied.
    Axis x, y, z, a, b;
    int extrusion_channel;
};

struct Machine {
    std::array<Head, head_count> heads;
    Axis bed_z;
    double max_tilt;
    double target_speed;
    double target_acceleration;
};

// The owning ConfigDef supplies its protected registration operation.
using OptionRegistrar = std::function<ConfigOptionDef*(const std::string&, ConfigOptionType)>;
void register_options(const OptionRegistrar& add);
const std::vector<std::string>& option_keys();
std::string head_key(std::size_t index, const std::string& suffix);
bool enabled(const ConfigOptionResolver& config);
Machine from_config(const ConfigOptionResolver& config);
// Empty means valid. This checks configuration envelopes, not collisions/reachability.
std::string validate(const Machine& machine);
const char* export_block_reason();
void require_planar_mode(const ConfigOptionResolver& config);

} // namespace Formorix5D
} // namespace Slic3r
