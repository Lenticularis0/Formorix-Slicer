// Formorix Slicer additions, licensed under AGPL-3.0-or-later.
#include "Formorix5D.hpp"

#include "Config.hpp"
#include "I18N.hpp"

#include <cmath>
#include <initializer_list>
#include <set>
#include <stdexcept>

// Extraction marker; these definitions are translated by Orca's settings UI.
#define L(s) s

namespace Slic3r::Formorix5D {
namespace {
struct AxisDefinition {
    const char* key;
    const char* label;
    const char* min_label;
    const char* max_label;
    double minimum;
    double maximum;
};

const std::array<AxisDefinition, 5> axes {{
    {"x", L("X position"), L("X minimum"), L("X maximum"), 0., 1600.},
    {"y", L("Y position"), L("Y minimum"), L("Y maximum"), 0., 350.},
    {"z", L("Local head Z position"), L("Local head Z minimum"), L("Local head Z maximum"), 0., 100.},
    {"a", L("A orientation"), L("A minimum"), L("A maximum"), -75., 75.},
    {"b", L("B orientation"), L("B minimum"), L("B maximum"), -75., 75.}
}};

void add_float(ConfigDef& def, const std::string& key, const char* label, const char* unit,
               double value, double minimum, double maximum, const char* tooltip)
{
    auto* option = def.add(key, coFloat);
    option->label = label;
    option->category = L("Formorix 5D");
    option->tooltip = tooltip;
    option->sidetext = unit;
    option->mode = comSimple;
    option->min = minimum;
    option->max = maximum;
    option->set_default_value(new ConfigOptionFloat(value));
}

double read_float(const ConfigOptionResolver& config, const std::string& key)
{
    const auto* value = config.option<ConfigOptionFloat>(key);
    if (!value)
        throw std::invalid_argument("Invalid Formorix 5D numeric option: " + key);
    return value->value;
}

int read_int(const ConfigOptionResolver& config, const std::string& key)
{
    const auto* value = config.option<ConfigOptionInt>(key);
    if (!value)
        throw std::invalid_argument("Invalid Formorix 5D integer option: " + key);
    return value->value;
}

Axis read_axis(const ConfigOptionResolver& config, const std::string& key)
{
    return {read_float(config, key), read_float(config, key + "_min"), read_float(config, key + "_max")};
}

bool valid_axis(const Axis& axis)
{
    return std::isfinite(axis.position) && std::isfinite(axis.minimum) && std::isfinite(axis.maximum) &&
           axis.minimum < axis.maximum && axis.position >= axis.minimum && axis.position <= axis.maximum;
}
} // namespace

std::string head_key(std::size_t index, const std::string& suffix)
{
    return "formorix_5d_head_" + std::to_string(index + 1) + "_" + suffix;
}

const std::vector<std::string>& option_keys()
{
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> result {"formorix_5d_enabled", "formorix_5d_schema_version",
            "formorix_5d_bed_z", "formorix_5d_bed_z_min", "formorix_5d_bed_z_max",
            "formorix_5d_max_tilt", "formorix_5d_target_speed", "formorix_5d_target_acceleration"};
        for (std::size_t i = 0; i < head_count; ++i) {
            for (const auto& axis : axes) {
                result.push_back(head_key(i, axis.key));
                result.push_back(head_key(i, std::string(axis.key) + "_min"));
                result.push_back(head_key(i, std::string(axis.key) + "_max"));
            }
            result.push_back(head_key(i, "extrusion_channel"));
        }
        return result;
    }();
    return keys;
}

void register_options(ConfigDef& def)
{
    auto* mode = def.add("formorix_5d_enabled", coBool);
    mode->label = L("Experimental Formorix 5D mode");
    mode->category = L("Formorix 5D");
    mode->tooltip = L("Configuration only for three independent heads. Slicing, G-code export and sending print jobs are disabled in this mode. Disable it to use conventional OrcaSlicer slicing.");
    mode->mode = comSimple;
    mode->set_default_value(new ConfigOptionBool(false));

    auto* schema = def.add("formorix_5d_schema_version", coInt);
    schema->label = L("Formorix configuration version");
    schema->mode = comDevelop;
    schema->readonly = true;
    schema->min = 1;
    schema->set_default_value(new ConfigOptionInt(schema_version));

    const char* linear_tip = L("Experimental configuration in millimeters; no movement is generated. Head Z is local travel, independent of the shared bed Z travel and printable height.");
    const char* angular_tip = L("Experimental orientation in degrees. A and B are independent axis limits; rotation order, combined tilt and collision checks are not implemented.");
    add_float(def, "formorix_5d_bed_z", L("Shared bed Z position"), L("mm"), 0., 0., 100000., linear_tip);
    add_float(def, "formorix_5d_bed_z_min", L("Shared bed Z minimum"), L("mm"), 0., 0., 100000., linear_tip);
    add_float(def, "formorix_5d_bed_z_max", L("Shared bed Z maximum"), L("mm"), 350., 0., 100000., linear_tip);
    add_float(def, "formorix_5d_max_tilt", L("Maximum axis tilt"), L("°"), 75., 0.01, 180., angular_tip);
    add_float(def, "formorix_5d_target_speed", L("Target speed"), L("mm/s"), 500., 0.01, 100000.,
              L("Provisional 5D target speed. This does not change conventional slicing speeds."));
    add_float(def, "formorix_5d_target_acceleration", L("Target acceleration"), L("mm/s²"), 10000., 0.01, 1000000.,
              L("Provisional 5D target acceleration. This does not emit firmware commands."));

    for (std::size_t i = 0; i < head_count; ++i) {
        for (const auto& axis : axes) {
            const bool angular = axis.key[0] == 'a' || axis.key[0] == 'b';
            const char* unit = angular ? L("°") : L("mm");
            const char* tip = angular ? angular_tip : linear_tip;
            const double bound = angular ? 180. : 100000.;
            const auto key = head_key(i, axis.key);
            add_float(def, key, axis.label, unit, 0., -bound, bound, tip);
            add_float(def, key + "_min", axis.min_label, unit, axis.minimum, -bound, bound, tip);
            add_float(def, key + "_max", axis.max_label, unit, axis.maximum, -bound, bound, tip);
        }
        auto* channel = def.add(head_key(i, "extrusion_channel"), coInt);
        channel->label = L("Extrusion channel");
        channel->category = L("Formorix 5D");
        channel->tooltip = L("Unique zero-based logical extrusion channel for this head. This is not an OrcaSlicer extruder index or a firmware tool command.");
        channel->mode = comSimple;
        channel->min = 0;
        channel->max = 255;
        channel->set_default_value(new ConfigOptionInt(static_cast<int>(i)));
    }
}

bool enabled(const ConfigOptionResolver& config)
{
    const auto* option = config.option<ConfigOptionBool>("formorix_5d_enabled");
    return option != nullptr && option->value;
}

Machine from_config(const ConfigOptionResolver& config)
{
    // A preset or full print config must contain all registered keys. Never reinterpret
    // a future schema as machine coordinates from this version.
    for (const auto& key : option_keys())
        if (config.option(key) == nullptr)
            throw std::invalid_argument("Missing Formorix 5D option: " + key);
    if (read_int(config, "formorix_5d_schema_version") != schema_version)
        throw std::invalid_argument("Unsupported Formorix 5D configuration version");
    Machine machine {};
    machine.bed_z = read_axis(config, "formorix_5d_bed_z");
    machine.max_tilt = read_float(config, "formorix_5d_max_tilt");
    machine.target_speed = read_float(config, "formorix_5d_target_speed");
    machine.target_acceleration = read_float(config, "formorix_5d_target_acceleration");
    for (std::size_t i = 0; i < head_count; ++i) {
        machine.heads[i] = {read_axis(config, head_key(i, "x")), read_axis(config, head_key(i, "y")),
            read_axis(config, head_key(i, "z")), read_axis(config, head_key(i, "a")),
            read_axis(config, head_key(i, "b")), read_int(config, head_key(i, "extrusion_channel"))};
    }
    return machine;
}

std::string validate(const Machine& machine)
{
    if (!valid_axis(machine.bed_z) || machine.bed_z.minimum < 0.)
        return L("Formorix 5D: the shared bed Z position must be within valid non-negative travel limits.");
    if (!std::isfinite(machine.max_tilt) || machine.max_tilt <= 0. || machine.max_tilt > 180. ||
        !std::isfinite(machine.target_speed) || machine.target_speed <= 0. ||
        !std::isfinite(machine.target_acceleration) || machine.target_acceleration <= 0.)
        return L("Formorix 5D: tilt, target speed and target acceleration must be finite positive values; tilt cannot exceed 180 degrees.");
    std::set<int> channels;
    for (const auto& head : machine.heads) {
        for (const auto& axis : {head.x, head.y, head.z, head.a, head.b})
            if (!valid_axis(axis))
                return L("Formorix 5D: every head position must be within finite limits, with minimum less than maximum.");
        if (head.a.minimum < -machine.max_tilt || head.a.maximum > machine.max_tilt ||
            head.b.minimum < -machine.max_tilt || head.b.maximum > machine.max_tilt)
            return L("Formorix 5D: head orientation limits exceed the maximum axis tilt.");
        if (head.extrusion_channel < 0 || head.extrusion_channel > 255 || !channels.insert(head.extrusion_channel).second)
            return L("Formorix 5D: each head needs a distinct extrusion channel between 0 and 255.");
    }
    return {};
}

const char* export_block_reason()
{
    return L("Formorix 5D is experimental and configuration-only. Slicing, executable G-code export and sending print jobs are disabled. Save a project or printer preset instead, or disable Formorix 5D mode for conventional slicing.");
}

void require_planar_mode(const ConfigOptionResolver& config)
{
    if (enabled(config))
        throw std::runtime_error(I18N::translate(export_block_reason()));
}

} // namespace Slic3r::Formorix5D
