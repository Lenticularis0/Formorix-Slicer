// Geometric underlay evidence and local requests; no strength or scheduling approval.
#pragma once
#include "FormorixCorePreview.hpp"
#include "FormorixHybrid.hpp"
#include "Point.hpp"
#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace Slic3r::FormorixUnderlay {
enum class Status { Complete, InvalidInput, ResourceLimit, Canceled };
struct Options
{
    double max_sample_edge  = .5; // mm, longest edge of each finite triangular sample footprint
    double search_distance  = .5; // mm along the inward interface normal
    std::size_t max_samples = 150000, max_faces = 250000, max_subdivisions = 1000000;
    std::size_t max_layer_candidates = 1000000, max_polygon_vertices = 1000000, max_requests = 256;
    std::size_t max_edge_tests = 50000000;
    // Native modifier ceilings; callers may lower, never exceed, these limits.
    std::size_t max_request_vertices = 2048, max_total_request_vertices = 65536;
};
struct Probe
{
    std::size_t source_interface = 0, source_face = 0;
    std::array<double, 3> barycentric{{0., 0., 0.}};
    std::array<Vec3d, 3> footprint;
    Vec3d position = Vec3d::Zero(), outward = Vec3d::Zero(); // outward from core toward skin
    double area = 0.;                                        // mm^2; finite leaf area, not a contact-area certificate
};
struct PreparedRegion
{
    int region = 0;
    std::vector<Probe> probes;
    double interface_area          = 0.;
    bool external_support_required = false; // no core interface; no synthetic contact samples
};
struct Preparation
{
    Status status = Status::InvalidInput;
    std::vector<PreparedRegion> regions;
    std::string error;
    bool ok() const { return status == Status::Complete; }
};
enum class RawKind { Contact, PathGap, NativeLayerGap, Unknown };
struct SampleEvidence
{
    std::size_t probe = 0, layer = std::numeric_limits<std::size_t>::max();
    RawKind kind            = RawKind::Unknown;
    double native_prism_gap = std::numeric_limits<double>::infinity();
    double raw_gap          = std::numeric_limits<double>::infinity();
};
struct RegionEvidence
{
    int region = 0;
    std::vector<SampleEvidence> samples;
    double interface_area = 0., raw_missing_area = 0., native_layer_gap_area = 0., path_gap_area = 0., unknown_area = 0.;
    std::size_t native_layer_gaps = 0, path_gaps = 0, unknown = 0;
    bool external_support_required = false;
    bool coverage_bounded          = false; // finite sampling never proves a whole footprint or mechanical support
};
struct Analysis
{
    Status status = Status::InvalidInput;
    std::vector<RegionEvidence> regions;
    std::vector<FormorixCorePreview::DenseRequest> dense_requests;
    std::vector<std::string> warnings;
    std::size_t layer_candidates = 0;
    std::size_t edge_tests       = 0;
    std::string error;
    bool ok() const { return status == Status::Complete; }
};
// Atomically replace legacy interface samples with area-weighted samples of the
// actual shared core(0)/skin interfaces. Each sample retains its source triangle.
Preparation prepare(FormorixHybrid::Result&, const Options& = {}, const std::function<bool()>& canceled = {});
// Call AFTER FormorixHybrid::audit_underlay has populated the matching contacts.
// Layer polygons use scaled original XY; print Z determines actual prism contact.
// Request Z remains geometric source Z for native modifiers. No completed work,
// material compatibility, calibrated nonunit-Flow geometry or support is invented.
Analysis analyze(FormorixHybrid::Result&,
                 const Preparation&,
                 const FormorixCorePreview::Result&,
                 const Options&                        = {},
                 const std::function<bool()>& canceled = {});
} // namespace Slic3r::FormorixUnderlay
