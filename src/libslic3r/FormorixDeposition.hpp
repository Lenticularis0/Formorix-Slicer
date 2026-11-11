// Nominal coverage accounting, not a calibrated extrusion/flow recommendation.
#pragma once

namespace Slic3r::FormorixProcess {
struct Profile;
struct CoverageCell
{
    double pitch                 = 0.;
    double supplied_area         = 0.; // mm^2, nominal ellipse area times configured flow ratio
    double union_area            = 0.; // mm^2 per pitch cell in an infinite aligned equal-bead row
    double excess_area           = 0.; // supplied minus union; positive means duplicate material volume
    double volume_balance_factor = 0.; // bookkeeping only; not automatically applied to extrusion
};
// Horizontal, aligned, equal-profile cross-sections. Does not model mixed-width
// boundaries, tilt/curvature, wetting, bead deformation or physical adhesion.
CoverageCell planar_coverage(const Profile&, double pitch);

// Explicit nominal coalescence model, distinct from the legacy rigid ellipse.
// Orca's non-bridge rounded-rectangle area supplies material to a compacted
// rectangular unit cell of the same volume. This is not a measured bead shape,
// a physical adhesion certificate, or permission to ignore finite path boundaries.
struct RibbonSection
{
    unsigned model_version  = 1;
    double requested_width  = 0.; // unchanged Orca/profile line width
    double height           = 0.; // layer_height * configured height_correction
    double area_before_flow = 0.; // rounded rectangle with configured width/height corrections
    double supplied_area    = 0.; // mm^3/mm, including the UNCHANGED configured flow ratio
    double compacted_width  = 0.; // supplied_area / height; not a second requested line width
};
// Requires corrected width >= corrected height. Callers must separately account
// for tilt, curvature, finite endpoints, turns and interfaces between materials.
RibbonSection ribbon_section(const Profile&);
// A different width requires explicit limits. Does not mutate the nominal profile.
RibbonSection ribbon_section(const Profile&, double requested_width);
double ribbon_speed(const Profile&);
double ribbon_speed(const Profile&, double requested_width);

struct RibbonBalance
{
    double excess_volume       = 0.; // positive supplied minus target volume, mm^3
    double numerical_tolerance = 0.; // declared numerical residual plus relative arithmetic allowance
    bool inadmissible          = false;
    bool gross_oversupply      = false;
};
// A ledger, not a correction: all volumes and the configured flow remain intact.
// The rounding residual is signed; supplied must be >=0 and target strictly >0.
// No excess beyond the declared numerical tolerance is treated as admissible.
RibbonBalance ribbon_balance(double supplied, double target, double rounding_residual = 0.);
} // namespace Slic3r::FormorixProcess
