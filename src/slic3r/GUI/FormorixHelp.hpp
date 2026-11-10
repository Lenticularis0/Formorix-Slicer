#pragma once
#include <wx/string.h>
class wxWindow;
namespace Slic3r::GUI {
enum class FormorixHelpTopic {
    Surfaces,
    Selection,
    SurfaceLayerHeight,
    ShellDepth,
    Normal,
    NozzleDeviation,
    Supports,
    ContinuousPaths,
    Transitions,
    MultiHead,
    HighSpeed,
    Preview
};
wxString formorix_help_url(FormorixHelpTopic topic);
wxWindow* formorix_help_button(wxWindow* parent, FormorixHelpTopic topic);
} // namespace Slic3r::GUI
