#ifndef ENGINE_UI_H
#define ENGINE_UI_H

/*
	The small immediate mode widget set the editor draws with. It is deliberately
	tiny: the painter already provides the vector drawing, this only adds the
	layout and the hit testing of a few widgets.
*/

#include <string>

#include <sugar_types.h>

#include "base/math.h"
#include "engine/input.h"
#include "engine/painter.h"

namespace engine::ui
{
	using painter::CColor;
	using painter::CPaint;
	using painter::CPainter;
	using painter::RGB;
	using painter::RGBA;
	using painter::RGBAf;
	using painter::RGBf;
	using painter::WithAlpha;

	struct CTheme
	{
		CColor m_Background = RGB(24, 26, 30);
		CColor m_Panel = RGB(34, 37, 43);
		CColor m_PanelDark = RGB(28, 30, 35);
		CColor m_Border = RGB(58, 63, 72);
		CColor m_Text = RGB(226, 229, 234);
		CColor m_TextDim = RGB(140, 146, 158);
		CColor m_Accent = RGB(94, 165, 255);
		CColor m_AccentDim = RGB(58, 96, 150);
		CColor m_Warning = RGB(240, 180, 90);
		CColor m_Error = RGB(235, 96, 96);
		CColor m_Success = RGB(120, 205, 130);
		CColor m_Hover = RGB(48, 53, 62);
		CColor m_Selection = RGB(56, 92, 148);
	};

	extern const CTheme Theme;

	void Rect(CPainter *pPainter, const base::rect &Rect, CColor Color);
	void RectOutline(CPainter *pPainter, const base::rect &Rect, CColor Color, float Width = 1.0f);
	void Line(CPainter *pPainter, base::vec2 From, base::vec2 To, CColor Color, float Width = 1.0f);

	enum class text_align
	{
		left,
		center,
		right,
	};

	void Text(CPainter *pPainter, int Font, base::vec2 Position, float Size, CColor Color, const std::string &String, text_align Align = text_align::left);
	// draws the text shortened with an ellipsis so it fits into Width
	void TextClipped(CPainter *pPainter, int Font, base::vec2 Position, float Size, CColor Color, const std::string &String, float Width, text_align Align = text_align::left);
	float Measure(CPainter *pPainter, int Font, float Size, const std::string &String);

	bool Button(CPainter *pPainter, const CInput &Input, int Font, const base::rect &Area, const std::string &Label, bool Active = false, bool Enabled = true);
	bool Checkbox(CPainter *pPainter, const CInput &Input, const base::rect &Area, bool &Value);
	bool Slider(CPainter *pPainter, const CInput &Input, const base::rect &Area, float &Value, float Min, float Max);

	// the mouse is inside the rect and no other widget grabbed the click
	bool Hovered(const CInput &Input, const base::rect &Area);
} // namespace engine::ui

#endif // ENGINE_UI_H
