#pragma once
#include "libslic3r/FormorixPreview.hpp"
#include "GLModel.hpp"
#include <libslic3r/Point.hpp>
#include <wx/scrolwin.h>
#include <wx/string.h>
#include <functional>
#include <memory>
#include <vector>

namespace Slic3r::FormorixHybrid { struct Result; }
namespace Slic3r::FormorixCorePreview { struct Result; }
namespace Slic3r::FormorixUnderlay { struct Preparation; struct Analysis; }
namespace Slic3r::GUI {
struct Camera;
struct FormorixPreviewState
{
    std::shared_ptr<const FormorixPreview::Result> result;
    std::shared_ptr<const FormorixHybrid::Result> hybrid;
    std::shared_ptr<const FormorixCorePreview::Result> conventional;
    std::shared_ptr<const FormorixUnderlay::Preparation> underlay_preparation;
    std::shared_ptr<const FormorixUnderlay::Analysis> underlay_before, underlay_after;
    double seconds = 0.;
    int layer      = -1;
    bool shell = false, core = false;
    bool original = false, core_paths = true, surface_paths = true;
    bool interfaces = false, missing_underlay = true, unverified = true;
};
wxString formorix_preview_error(FormorixPreview::Issue issue);
class FormorixPreviewPanel : public wxScrolledWindow
{
public:
    FormorixPreviewPanel(wxWindow* parent, std::function<void()> changed, std::function<void()> ordinary);
    ~FormorixPreviewPanel() override;
    std::shared_ptr<FormorixPreviewState> state() const;
    void set_result(std::shared_ptr<const FormorixPreview::Result>);
    void set_hybrid(std::shared_ptr<const FormorixHybrid::Result>, std::shared_ptr<const FormorixCorePreview::Result>,
                    std::shared_ptr<const FormorixUnderlay::Preparation> = {},
                    std::shared_ptr<const FormorixUnderlay::Analysis> before = {},
                    std::shared_ptr<const FormorixUnderlay::Analysis> after = {});
    void pause();
    void rescale();
    void theme();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
// Only called from the existing canvas render pass, with its GL context current.
class FormorixPreviewRenderer
{
public:
    void render(const FormorixPreviewState&, const Camera&);

private:
    std::shared_ptr<const FormorixPreview::Result> m_result;
    std::shared_ptr<const FormorixHybrid::Result> m_hybrid;
    std::shared_ptr<const FormorixCorePreview::Result> m_conventional;
    struct PathDisplay
    {
        int layer = 0, head = 0;
        GLModel path, arrows;
        std::vector<double> ends;
        Vec3d start, finish;
    };
    std::vector<std::unique_ptr<PathDisplay>> m_paths;
    std::vector<std::unique_ptr<GLModel>> m_shells;
    GLModel m_core, m_invalid, m_marker, m_active, m_axis;
    GLModel m_original, m_interfaces, m_missing_underlay, m_unverified;
    GLModel m_unsuitable_infill, m_underlay_gaps, m_unknown_underlay;
    std::vector<std::unique_ptr<GLModel>> m_core_paths;
    void rebuild(const FormorixPreview::Result&);
    void rebuild_hybrid(const FormorixPreviewState&);
};
} // namespace Slic3r::GUI
