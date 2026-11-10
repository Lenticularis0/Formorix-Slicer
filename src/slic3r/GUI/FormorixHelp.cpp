#include "FormorixHelp.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Widgets/Button.hpp"
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/window.h>
#include <array>
#include <cstddef>

namespace Slic3r::GUI {
wxString formorix_help_url(FormorixHelpTopic topic)
{
    // One mapping can later switch the whole GUI to a GitHub Wiki without changing controls.
    static const std::array<const char*, 12> pages{{"index.md", "selection.md", "layers.md#schichtdicke", "layers.md#schalendicke",
                                                    "orientation.md#oberflachennormale", "orientation.md#abweichung-und-kippwinkel",
                                                    "supports.md", "continuous-paths.md", "transitions.md", "multi-head.md",
                                                    "high-speed.md", "preview.md"}};
    // Pin published documentation to a commit: deleting the development branch cannot break help.
    return "https://github.com/Lenticularis0/Formorix-Slicer/blob/10177f248099015508246693bb67d8a5b0357101/docs/formorix/" +
           wxString::FromUTF8(pages.at(static_cast<std::size_t>(topic)));
}
wxWindow* formorix_help_button(wxWindow* parent, FormorixHelpTopic topic)
{
    auto* button = new Button(parent, "?");
    button->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
    button->SetMinSize(parent->FromDIP(wxSize(24, 24)));
    button->SetToolTip(_L("Open Formorix help"));
    button->Bind(wxEVT_BUTTON, [button, topic](wxCommandEvent&) {
        button->CallAfter([topic] {
            if (!wxGetApp().is_closing())
                wxGetApp().open_browser_with_warning_dialog(formorix_help_url(topic));
        });
    });
    return button;
}
} // namespace Slic3r::GUI
