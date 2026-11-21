// The colours of everything the mod draws: the F10 menu and its pages, the start card, the notice box and the performance
// panels. They are the tokens of the website (styles.css, :root), so the menu and the site look like one thing: neutral greys,
// one red, no second accent. Before, five files kept their own copy of a bluish palette with a green for every switch.
#pragma once
#include "../base.h"

namespace ufx {
namespace ui {
namespace theme {

// Surfaces, darkest first.
constexpr u32 kBg = 0xFF0B0B0C;        // --bg: the sidebar, the well of the number box
constexpr u32 kPanel = 0xFF111113;     // --panel: the menu panel (its alpha comes from the Opacity setting)
constexpr u32 kRaised = 0xFF151517;    // --panel-2: cards, the notice, the dialog, quiet buttons
constexpr u32 kSelected = 0xFF1F1F23;  // the selected row and page (the site's active tab)
constexpr u32 kHover = 0xFF26262A;     // a button under the mouse, the chosen language
// Lines.
constexpr u32 kLine = 0xFF1E1E21;      // --line: hairlines between parts, scroll tracks
constexpr u32 kLine2 = 0xFF2A2A2E;     // --line-2: borders of controls, a switch that is off, a slider track
// Text.
constexpr u32 kWhite = 0xFFFFFFFF;     // the selected row's label, text on red, the knob of a switch
constexpr u32 kText = 0xFFEDEDEE;      // --text
constexpr u32 kMuted = 0xFFA1A1A8;     // --muted: descriptions, labels of buttons that are not chosen
constexpr u32 kFaint = 0xFF8A8A92;     // --faint: key hints, versions, secondary columns
// The one accent, in three tints. kAccent and kAccentHi are fill colours. As text even kAccentHi is only 3.9:1 on the panel
// and 3.4:1 on the selected row, and the 13 px headings need 4.5:1 (WCAG for small text), so red text has its own lighter
// tint: 5.9:1 on the panel, 5.2:1 on the selected row. Changing kAccentHi instead would also lighten the focused buttons.
constexpr u32 kAccent = 0xFFC4192B;    // --accent: a switch that is on, the chosen button, selection bars
constexpr u32 kAccentHi = 0xFFD92A3C;  // --accent-hi: a pressed or focused button, a hovered chosen one
constexpr u32 kAccentText = 0xFFF0606C;  // red text: headings, a focused value
// Status, kept for what they say and nothing else.
constexpr u32 kWarn = 0xFFE5A83A;      // amber: only the "restart" tag, what waits for a restart, the mark of a warning notice
constexpr u32 kError = 0xFFFF8A80;     // coral: only failures (also the mark of a notice that reports one)

// The same colour with another alpha byte (0..255).
constexpr u32 Alpha(u32 argb, u32 alpha) { return (argb & 0x00FFFFFFu) | (alpha << 24); }

}  // namespace theme
}  // namespace ui
}  // namespace ufx
