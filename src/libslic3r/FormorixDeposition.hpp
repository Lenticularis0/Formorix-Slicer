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
} // namespace Slic3r::FormorixProcess
