#ifndef ENGINE_UI_H
#define ENGINE_UI_H

/*
	The small immediate mode widget set the editor draws with. It is deliberately
	tiny: nanovg already provides the vector drawing, this only adds the layout
	and the hit testing of a few widgets.
*/

#include <string>

#include <nanovg.h>
#include <sugar_types.h>

#include "base/math.h"
#include "engine/input.h"

namespace engine::ui
{
	struct CTheme
	{
		NVGcolor m_Background = nvgRGB(24, 26, 30);
		NVGcolor m_Panel = nvgRGB(34, 37, 43);
		NVGcolor m_PanelDark = nvgRGB(28, 30, 35);
		NVGcolor m_Border = nvgRGB(58, 63, 72);
		NVGcolor m_Text = nvgRGB(226, 229, 234);
		NVGcolor m_TextDim = nvgRGB(140, 146, 158);
		NVGcolor m_Accent = nvgRGB(94, 165, 255);
		NVGcolor m_AccentDim = nvgRGB(58, 96, 150);
		NVGcolor m_Warning = nvgRGB(240, 180, 90);
		NVGcolor m_Error = nvgRGB(235, 96, 96);
		NVGcolor m_Success = nvgRGB(120, 205, 130);
		NVGcolor m_Hover = nvgRGB(48, 53, 62);
		NVGcolor m_Selection = nvgRGB(56, 92, 148);
	};

	extern const CTheme Theme;

	void Rect(NVGcontext *pContext, const base::rect &Rect, NVGcolor Color);
	void RectOutline(NVGcontext *pContext, const base::rect &Rect, NVGcolor Color, float Width = 1.0f);
	void Line(NVGcontext *pContext, base::vec2 From, base::vec2 To, NVGcolor Color, float Width = 1.0f);

	enum class text_align
	{
		left,
		center,
		right,
	};

	void Text(NVGcontext *pContext, int Font, base::vec2 Position, float Size, NVGcolor Color, const std::string &String, text_align Align = text_align::left);
	// draws the text shortened with an ellipsis so it fits into Width
	void TextClipped(NVGcontext *pContext, int Font, base::vec2 Position, float Size, NVGcolor Color, const std::string &String, float Width, text_align Align = text_align::left);
	float Measure(NVGcontext *pContext, int Font, float Size, const std::string &String);

	bool Button(NVGcontext *pContext, const CInput &Input, int Font, const base::rect &Area, const std::string &Label, bool Active = false, bool Enabled = true);
	bool Checkbox(NVGcontext *pContext, const CInput &Input, const base::rect &Area, bool &Value);
	bool Slider(NVGcontext *pContext, const CInput &Input, const base::rect &Area, float &Value, float Min, float Max);

	// the mouse is inside the rect and no other widget grabbed the click
	bool Hovered(const CInput &Input, const base::rect &Area);
} // namespace engine::ui

#endif // ENGINE_UI_H
