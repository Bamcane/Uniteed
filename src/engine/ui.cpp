#include "ui.h"

#include <algorithm>

namespace engine::ui
{
	namespace
	{
		// the painter's alignment values: 0 left, 1 centre, 2 right
		int AlignValue(text_align Align)
		{
			return Align == text_align::left ? 0 : (Align == text_align::center ? 1 : 2);
		}
	} // namespace

	void Rect(CPainter *pPainter, const base::rect &Area, CColor Color)
	{
		pPainter->FillRect(Area.m_TopLeft.x, Area.m_TopLeft.y, Area.Width(), Area.Height(), Color);
	}

	void RectOutline(CPainter *pPainter, const base::rect &Area, CColor Color, float Width)
	{
		pPainter->StrokeRect(Area.m_TopLeft.x + Width * 0.5f, Area.m_TopLeft.y + Width * 0.5f,
			Area.Width() - Width, Area.Height() - Width, Color, Width);
	}

	void Line(CPainter *pPainter, base::vec2 From, base::vec2 To, CColor Color, float Width)
	{
		pPainter->DrawLine(From.x, From.y, To.x, To.y, Color, Width);
	}

	float Measure(CPainter *pPainter, int Font, float Size, const std::string &String)
	{
		return pPainter->MeasureString(Font, Size, String);
	}

	void Text(CPainter *pPainter, int Font, base::vec2 Position, float Size, CColor Color, const std::string &String, text_align Align)
	{
		pPainter->FontFaceId(Font);
		pPainter->FontSize(Size);
		pPainter->FillColor(Color);
		pPainter->TextAlign(AlignValue(Align));
		pPainter->Text(Position.x, Position.y, String);
	}

	void TextClipped(CPainter *pPainter, int Font, base::vec2 Position, float Size, CColor Color, const std::string &String, float Width, text_align Align)
	{
		std::string Shown = String;
		if(Measure(pPainter, Font, Size, Shown) > Width)
		{
			while(!Shown.empty() && Measure(pPainter, Font, Size, Shown + "...") > Width)
				Shown.pop_back();
			Shown += "...";
		}
		Text(pPainter, Font, Position, Size, Color, Shown, Align);
	}

	bool Hovered(const CInput &Input, const base::rect &Area)
	{
		// a blocked mouse means an overlay is being drawn over this widget
		return !Input.MouseBlocked() && Area.Contains(Input.MousePosition());
	}

	bool Button(CPainter *pPainter, const CInput &Input, int Font, const base::rect &Area, const std::string &Label, bool Active, bool Enabled)
	{
		const bool bHovered = Enabled && Hovered(Input, Area);
		CColor Background = Theme.m_Panel;
		if(Active)
			Background = Theme.m_AccentDim;
		else if(bHovered)
			Background = Theme.m_Hover;

		Rect(pPainter, Area, Background);
		RectOutline(pPainter, Area, Active ? Theme.m_Accent : Theme.m_Border);
		Text(pPainter, Font, base::vec2(Area.Center().x, Area.Center().y + 4.0f), 13.0f,
			Enabled ? Theme.m_Text : Theme.m_TextDim, Label, text_align::center);
		return Enabled && bHovered && Input.MousePressed(CInput::mouse_button::left);
	}

	void Tooltip(CPainter *pPainter, int FontBold, int Font, base::vec2 Position, const std::string &Title,
		const std::string &Detail, const base::rect &Window)
	{
		const float Padding = 9.0f;
		const float TitleSize = 12.5f;
		const float DetailSize = 11.5f;
		float Width = Measure(pPainter, FontBold, TitleSize, Title);
		if(!Detail.empty())
			Width = std::max(Width, Measure(pPainter, Font, DetailSize, Detail));
		Width += Padding * 2.0f;
		const float Height = Detail.empty() ? 24.0f : 40.0f;

		// the cursor is usually at the top left of the widget, so the hint
		// opens down and to the right and only moves when there is no room
		Position.x = base::clamp(Position.x, Window.m_TopLeft.x + 2.0f,
			std::max(Window.m_BottomRight.x - Width - 2.0f, Window.m_TopLeft.x + 2.0f));
		Position.y = base::clamp(Position.y, Window.m_TopLeft.y + 2.0f,
			std::max(Window.m_BottomRight.y - Height - 2.0f, Window.m_TopLeft.y + 2.0f));

		const base::rect Area = base::rect::FromSize(Position, base::vec2(Width, Height));
		Rect(pPainter, Area, Theme.m_PanelLight);
		RectOutline(pPainter, Area, Theme.m_Border);
		Text(pPainter, FontBold, base::vec2(Area.m_TopLeft.x + Padding, Area.m_TopLeft.y + 16.0f), TitleSize,
			Theme.m_Text, Title);
		if(!Detail.empty())
			TextClipped(pPainter, Font, base::vec2(Area.m_TopLeft.x + Padding, Area.m_TopLeft.y + 31.0f), DetailSize,
				Theme.m_TextDim, Detail, Width - Padding * 2.0f);
	}

	bool Checkbox(CPainter *pPainter, const CInput &Input, const base::rect &Area, bool &Value)
	{
		const bool bHovered = Hovered(Input, Area);
		Rect(pPainter, Area, bHovered ? Theme.m_Hover : Theme.m_PanelDark);
		RectOutline(pPainter, Area, Theme.m_Border);
		if(Value)
		{
			pPainter->BeginPath();
			pPainter->MoveTo(Area.m_TopLeft.x + 4.0f, Area.Center().y);
			pPainter->LineTo(Area.Center().x - 1.0f, Area.m_BottomRight.y - 4.0f);
			pPainter->LineTo(Area.m_BottomRight.x - 4.0f, Area.m_TopLeft.y + 4.0f);
			pPainter->StrokeColor(Theme.m_Accent);
			pPainter->StrokeWidth(2.0f);
			pPainter->Stroke();
		}
		if(bHovered && Input.MousePressed(CInput::mouse_button::left))
		{
			Value = !Value;
			return true;
		}
		return false;
	}

	bool Slider(CPainter *pPainter, const CInput &Input, const base::rect &Area, float &Value, float Min, float Max)
	{
		const float Span = (Max > Min) ? (Max - Min) : 1.0f;
		const float Fraction = base::clamp((Value - Min) / Span, 0.0f, 1.0f);
		Rect(pPainter, Area, Theme.m_PanelDark);
		Rect(pPainter, base::rect(Area.m_TopLeft, base::vec2(Area.m_TopLeft.x + Area.Width() * Fraction, Area.m_BottomRight.y)), Theme.m_AccentDim);
		RectOutline(pPainter, Area, Theme.m_Border);

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
