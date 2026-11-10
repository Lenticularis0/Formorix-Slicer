#pragma once
#include "libslic3r/FormorixPreview.hpp"
#include "GLModel.hpp"
#include <libslic3r/Point.hpp>
#include <wx/panel.h>
#include <wx/string.h>
#include <functional>
#include <memory>
#include <vector>

namespace Slic3r::GUI {
struct Camera;
struct FormorixPreviewState
{
    std::shared_ptr<const FormorixPreview::Result> result;
    double seconds = 0.;
    int layer      = -1;
    bool shell = false, core = false;
};
wxString formorix_preview_error(FormorixPreview::Issue issue);
class FormorixPreviewPanel : public wxPanel
{
public:
    FormorixPreviewPanel(wxWindow* parent, std::function<void()> changed, std::function<void()> ordinary);
    ~FormorixPreviewPanel() override;
    std::shared_ptr<FormorixPreviewState> state() const;
    void set_result(std::shared_ptr<const FormorixPreview::Result>);
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
    void rebuild(const FormorixPreview::Result&);
};
} // namespace Slic3r::GUI
