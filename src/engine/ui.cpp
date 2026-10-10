#include "ui.h"

#include <algorithm>

namespace engine::ui
{
	const CTheme Theme;

	void Rect(NVGcontext *pContext, const base::rect &Area, NVGcolor Color)
	{
		nvgBeginPath(pContext);
		nvgRect(pContext, Area.m_TopLeft.x, Area.m_TopLeft.y, Area.Width(), Area.Height());
		nvgFillColor(pContext, Color);
		nvgFill(pContext);
	}

	void RectOutline(NVGcontext *pContext, const base::rect &Area, NVGcolor Color, float Width)
	{
		nvgBeginPath(pContext);
		nvgRect(pContext, Area.m_TopLeft.x + Width * 0.5f, Area.m_TopLeft.y + Width * 0.5f, Area.Width() - Width, Area.Height() - Width);
		nvgStrokeColor(pContext, Color);
		nvgStrokeWidth(pContext, Width);
		nvgStroke(pContext);
	}

	void Line(NVGcontext *pContext, base::vec2 From, base::vec2 To, NVGcolor Color, float Width)
	{
		nvgBeginPath(pContext);
		nvgMoveTo(pContext, From.x, From.y);
		nvgLineTo(pContext, To.x, To.y);
		nvgStrokeColor(pContext, Color);
		nvgStrokeWidth(pContext, Width);
		nvgStroke(pContext);
	}

	float Measure(NVGcontext *pContext, int Font, float Size, const std::string &String)
	{
		nvgFontFaceId(pContext, Font);
		nvgFontSize(pContext, Size);
		return nvgTextBounds(pContext, 0.0f, 0.0f, String.c_str(), nullptr, nullptr);
	}

	void Text(NVGcontext *pContext, int Font, base::vec2 Position, float Size, NVGcolor Color, const std::string &String, text_align Align)
	{
		nvgFontFaceId(pContext, Font);
		nvgFontSize(pContext, Size);
		nvgFillColor(pContext, Color);
		nvgTextAlign(pContext, Align == text_align::left ? NVG_ALIGN_LEFT : (Align == text_align::center ? NVG_ALIGN_CENTER : NVG_ALIGN_RIGHT));
		nvgText(pContext, Position.x, Position.y, String.c_str(), nullptr);
	}

	void TextClipped(NVGcontext *pContext, int Font, base::vec2 Position, float Size, NVGcolor Color, const std::string &String, float Width, text_align Align)
	{
		std::string Shown = String;
		if(Measure(pContext, Font, Size, Shown) > Width)
		{
			while(!Shown.empty() && Measure(pContext, Font, Size, Shown + "...") > Width)
				Shown.pop_back();
			Shown += "...";
		}
		Text(pContext, Font, Position, Size, Color, Shown, Align);
	}

	bool Hovered(const CInput &Input, const base::rect &Area)
	{
		return Area.Contains(Input.MousePosition());
	}

	bool Button(NVGcontext *pContext, const CInput &Input, int Font, const base::rect &Area, const std::string &Label, bool Active, bool Enabled)
	{
		const bool bHovered = Enabled && Hovered(Input, Area);
		NVGcolor Background = Theme.m_Panel;
		if(Active)
			Background = Theme.m_AccentDim;
		else if(bHovered)
			Background = Theme.m_Hover;

		Rect(pContext, Area, Background);
		RectOutline(pContext, Area, Active ? Theme.m_Accent : Theme.m_Border);
		Text(pContext, Font, base::vec2(Area.Center().x, Area.Center().y + 4.0f), 13.0f,
			Enabled ? Theme.m_Text : Theme.m_TextDim, Label, text_align::center);
		return Enabled && bHovered && Input.MousePressed(CInput::mouse_button::left);
	}

	bool Checkbox(NVGcontext *pContext, const CInput &Input, const base::rect &Area, bool &Value)
	{
		const bool bHovered = Hovered(Input, Area);
		Rect(pContext, Area, bHovered ? Theme.m_Hover : Theme.m_PanelDark);
		RectOutline(pContext, Area, Theme.m_Border);
		if(Value)
		{
			nvgBeginPath(pContext);
			nvgMoveTo(pContext, Area.m_TopLeft.x + 4.0f, Area.Center().y);
			nvgLineTo(pContext, Area.Center().x - 1.0f, Area.m_BottomRight.y - 4.0f);
			nvgLineTo(pContext, Area.m_BottomRight.x - 4.0f, Area.m_TopLeft.y + 4.0f);
			nvgStrokeColor(pContext, Theme.m_Accent);
			nvgStrokeWidth(pContext, 2.0f);
			nvgStroke(pContext);
		}
		if(bHovered && Input.MousePressed(CInput::mouse_button::left))
		{
			Value = !Value;
			return true;
		}
		return false;
	}

	bool Slider(NVGcontext *pContext, const CInput &Input, const base::rect &Area, float &Value, float Min, float Max)
	{
		const float Span = (Max > Min) ? (Max - Min) : 1.0f;
		const float Fraction = base::clamp((Value - Min) / Span, 0.0f, 1.0f);
		Rect(pContext, Area, Theme.m_PanelDark);
		Rect(pContext, base::rect(Area.m_TopLeft, base::vec2(Area.m_TopLeft.x + Area.Width() * Fraction, Area.m_BottomRight.y)), Theme.m_AccentDim);
		RectOutline(pContext, Area, Theme.m_Border);

		bool Changed = false;
		if(Hovered(Input, Area) && Input.MouseDown(CInput::mouse_button::left))
		{
			const float NewFraction = base::clamp((Input.MousePosition().x - Area.m_TopLeft.x) / std::max(Area.Width(), 1.0f), 0.0f, 1.0f);
			Value = Min + NewFraction * Span;
			Changed = true;
		}
		return Changed;
	}
} // namespace engine::ui
