#ifndef ENGINE_UI_THEME_H
#define ENGINE_UI_THEME_H

/*
	The interface colours, on their own so the parts of the editor that only
	need the palette do not have to pull in the painter.

	This is a Krita flavoured dark theme: a near neutral, very dark grey
	background so the map is the brightest thing on screen, slightly warmer
	panel greys stacked in three steps to make a dock visible without a heavy
	border, and a single saturated accent that marks the active tab, the
	selection and the focused field. Warnings and errors are desaturated so
	they do not compete with the artwork.
*/

#include "engine/painter.h"

namespace engine::ui
{
	// the painter's colour and its constructors, so this header needs nothing
	// but painter.h
	using painter::CColor;
	using painter::RGB;

	struct CTheme
	{
		/*
			Five steps of neutral grey, which is what a Krita workspace is built
			from: the window surround is the darkest thing on screen, the panels
			sit one step above it, and a raised strip (a toolbar, a tab row) is
			one step above that. The map is therefore always the brightest
			large area, which is the whole point of a dark editor theme.
		*/
		CColor m_Background = RGB(21, 22, 25);
		// the map viewport, a shade above the surround, so the boundary of the
		// workspace reads even when the map itself is nearly black
		CColor m_Workspace = RGB(28, 30, 34);
		CColor m_Panel = RGB(35, 37, 41);
		CColor m_PanelDark = RGB(28, 29, 33);
		// the raised strip a toolbar or a tab row is drawn with
		CColor m_PanelLight = RGB(44, 46, 51);
		CColor m_Border = RGB(56, 58, 64);
		CColor m_Text = RGB(232, 234, 238);
		CColor m_TextDim = RGB(150, 154, 162);
		// one saturated accent for the selection, the active tab and a focused
		// field, and a dim version for a band that should not shout
		CColor m_Accent = RGB(88, 166, 255);
		CColor m_AccentDim = RGB(46, 84, 138);
		CColor m_Warning = RGB(236, 178, 96);
		CColor m_Error = RGB(236, 104, 104);
		CColor m_Success = RGB(126, 206, 138);
		CColor m_Hover = RGB(50, 53, 60);
		CColor m_Selection = RGB(50, 86, 140);
		// a second, warmer accent for the things that are about the map rather
		// than about the interface: a physics layer, a sound source, a marker
		CColor m_MapAccent = RGB(226, 168, 96);
		// the envelope curve view
		CColor m_Curve = RGB(120, 200, 255);
		CColor m_CurveGrid = RGB(42, 44, 49);
		// the two channel pairs of the envelope view
		CColor m_ChannelA = RGB(240, 120, 120);
		CColor m_ChannelB = RGB(130, 220, 140);
	};

	extern const CTheme Theme;
} // namespace engine::ui

#endif // ENGINE_UI_THEME_H
