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
#include "engine/ui_theme.h"

namespace engine::ui
{
	using painter::address_mode;
	using painter::CColor;
	using painter::CPaint;
	using painter::CPainter;
	using painter::RGB;
	using painter::RGBA;
	using painter::RGBAf;
	using painter::RGBf;
	using painter::WithAlpha;


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

	/*
		A hint that names the widget under the cursor. It is drawn by the caller
		after everything else, because a tooltip that a panel can cover is worse
		than no tooltip: Position is where it would like to be, and it is moved
		back inside Area when it would leave the window.
	*/
	void Tooltip(CPainter *pPainter, int FontBold, int Font, base::vec2 Position, const std::string &Title,
		const std::string &Detail, const base::rect &Window);
	bool Checkbox(CPainter *pPainter, const CInput &Input, const base::rect &Area, bool &Value);
	bool Slider(CPainter *pPainter, const CInput &Input, const base::rect &Area, float &Value, float Min, float Max);

	// the mouse is inside the rect and no other widget grabbed the click
	bool Hovered(const CInput &Input, const base::rect &Area);
} // namespace engine::ui

#endif // ENGINE_UI_H
