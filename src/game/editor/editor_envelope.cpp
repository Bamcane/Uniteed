/*
	The envelope workspace: the visual curve editor for the envelopes of a map.
	It is a timeline rather than a bar - a curve view of one channel pair, the
	list of the points that build it, and the transport that drives the preview
	clock the layer panel and the map itself read.

	The whole file is one CEditor method plus its file local helpers, because
	the workspace is drawn in the same immediate mode pass as everything else:
	there is no retained state to keep in sync with the model, only the editor
	members the rest of the drawing already uses.
*/

#include <algorithm>
#include <cmath>

#include "base/i18n.h"
#include "base/string.h"
#include "base/time.h"
#include "engine/graphics.h"
#include "engine/keycodes.h"
#include "engine/ui.h"
#include "game/editor/editor.h"
#include "game/map/envelope.h"

namespace game
{
	using base::i18n::Tr;
	namespace ui = engine::ui;

	/*
		The few pieces of state an interaction needs to survive a frame. They
		are file local rather than CEditor members because editor.h is not this
		file's to change, and they are one shared instance because a process has
		one workspace showing at a time.
	*/
	namespace
	{
		struct CEnvelopeView
		{
			// the point list scrolls on its own: m_EnvelopeScroll is the time
			// axis of the curve view, and the two cannot share one value
			float m_ListScroll = 0.0f;
			// the grid is a view setting of this workspace, not a map property
			bool m_bGrid = true;
			/*
				The time zoom is the workspace's own, in pixels per second, and
				it starts unset: a fixed default cannot be right for both a 0.3s
				blink and a two minute fly-by, and at 1 px/s a four second
				envelope is four pixels wide, which reads as a flat line. The
				first frame that sees an envelope fits it instead, and only then
				does the wheel take over.
			*/
			bool m_bZoomSet = false;
		};
		CEnvelopeView g_Envelope;

		// the point being dragged, and where the cursor grabbed it: the drag
		// then writes the value under the cursor, which cannot accumulate
		// rounding the way a delta per frame would
		struct CEnvelopeDrag
		{
			int m_Point = -1;
			base::vec2 m_Grab;
			bool m_bPushed = false;
			bool m_bMoved = false;
		};
		CEnvelopeDrag g_EnvelopeDrag;

		// the curve view, in pixels per second: 0.05 shows ten minutes across a
		// dock, 200 shows ten milliseconds per centimetre
		constexpr float EnvelopeMinZoom = 0.05f;
		constexpr float EnvelopeMaxZoom = 200.0f;

		// a handle has to stay grabbable at every zoom, so it is a screen size
		constexpr float EnvelopeHandleSize = 9.0f;
		constexpr float EnvelopeHandleGrab = 8.0f;

		constexpr float EnvelopeRowHeight = 18.0f;

		// the shortcuts the workspace answers to, so the footnote and the key
		// handler cannot drift apart
		constexpr int EnvelopeKeyChannel = engine::keys::T;
		constexpr int EnvelopeKeyGrid = engine::keys::B;

		// i18n:table
		const char *const g_apCurveName[NUM_CURVETYPES] = {"step", "linear", "slow", "fast", "smooth", "bezier"};

		std::string CurveName(int Type)
		{
			if(Type < 0 || Type >= NUM_CURVETYPES)
				return "?";
			return Tr(g_apCurveName[Type]);
		}

		// one sample every two pixels: enough for a smooth trace, and cheap
		int EnvelopeSamples(float Width)
		{
			return base::clamp(static_cast<int>(std::ceil(std::max(Width, 1.0f) * 0.5f)), 2, 2048);
		}

		// an envelope needs one to four channels, whatever the buttons ask for
		int EnvelopeChannelCount(int Channels)
		{
			return base::clamp(Channels, 1, 4);
		}

		int ClampToInt(float Value)
		{
			if(Value <= -2147483000.0f)
				return -2147483000;
			if(Value >= 2147483000.0f)
				return 2147483000;
			return static_cast<int>(std::lround(Value));
		}

		// frames a time span in a plot, writing the zoom and the scroll
		void EnvelopeZoomToShow(float Span, float PlotWidth, int &Zoom, float &Scroll)
		{
			const float Wanted = PlotWidth / std::max(Span * 1.05f, 0.02f);
			Zoom = base::clamp(static_cast<int>(std::lround(Wanted)), static_cast<int>(EnvelopeMinZoom),
				static_cast<int>(EnvelopeMaxZoom));
			Scroll = 0.0f;
		}
	} // namespace

	/*
		The curve view of one envelope. The x axis is time, the y axis the fixed
		point value of a channel, and the two traces on it are the channel pair
		the header selected. Every point of the envelope is a handle, so the
		shape is edited where it is read rather than through a table of numbers.
	*/
	void CEditor::DrawEnvelopeWorkspace(ui::CPainter *pPainter, const base::rect &Area)
	{
		engine::CInput &Input = this->Input();
		ui::Rect(pPainter, Area, ui::Theme.m_Panel);
		ui::Line(pPainter, base::vec2(Area.m_TopLeft.x + 0.5f, Area.m_TopLeft.y),
			base::vec2(Area.m_TopLeft.x + 0.5f, Area.m_BottomRight.y), ui::Theme.m_Border);

		if(Area.Width() <= 40.0f || Area.Height() <= 90.0f)
			return; // a dock this small cannot hold a plot and a list

		// ------------------------------------------------------------- header
		const float X = Area.m_TopLeft.x + 8.0f;
		const float Width = Area.Width() - 16.0f;
		float y = Area.m_TopLeft.y + 6.0f;

		CEnvelope *pEnvelope = SelectedEnvelope();
		const int EnvelopeIndex = SelectedEnvelopeIndex();
		const int EnvelopeCount = static_cast<int>(m_Map.m_vEnvelopes.size());
		const int ChannelCount = pEnvelope != nullptr ? EnvelopeChannelCount(pEnvelope->m_Channels) : 0;

		// the pair is clamped here as well as at every write, because the
		// envelope may have lost channels since the last frame
		if(m_EnvelopeChannelBase > 0 && m_EnvelopeChannelBase >= ChannelCount)
			m_EnvelopeChannelBase = 0;

		ui::TextClipped(pPainter, m_pGraphics->FontBold(), base::vec2(X, y + 13.0f), 13.0f, ui::Theme.m_Text,  
			pEnvelope != nullptr ? (pEnvelope->m_Name.empty() ? Tr("unnamed envelope") : pEnvelope->m_Name) : Tr("no envelope")  ,
			Width - 84.0f);

		// which envelope: a cycling control, because a dock is too narrow for a
		// dropdown list of its own
		if(EnvelopeCount > 0)
		{
			const base::rect Cycle = base::rect::FromSize(base::vec2(Area.m_BottomRight.x - 76.0f, y), base::vec2(68.0f, 22.0f));
			if(ui::Button(pPainter, Input, m_pGraphics->Font(), Cycle, 
				base::FormatRuntime(Tr("{:d} / {:d}"), EnvelopeIndex + 1, EnvelopeCount) ))
			{
				m_SelectedEnvelope = (EnvelopeIndex + 1) % EnvelopeCount;
				m_SelectedEnvelopePoint = -1;
				g_Envelope.m_ListScroll = 0.0f;
				// another envelope is another length: frame it on the next frame
				g_Envelope.m_bZoomSet = false;
			}
			if(ui::Hovered(Input, Cycle))
				Hint(Cycle, Tr("envelope") , Tr("click to walk the envelopes of this map, one per press") );
		}
		else
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_BottomRight.x - 8.0f, y + 13.0f), 11.0f,
				ui::Theme.m_TextDim, Tr("none") , ui::text_align::right);
		y += 26.0f;

		ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(X, y + 12.0f), 11.0f, ui::Theme.m_TextDim,  
			pEnvelope != nullptr ? base::FormatRuntime(Tr("{:d} channels, {:d} points, {:.3f}s loop"), ChannelCount,
				pEnvelope->m_vPoints.size(), EnvelopeLength(*pEnvelope)) : Tr("create one to start")  , Width);
		y += 18.0f;

		// the buttons that create an envelope, and its channel count
		float ButtonX = X;
		const auto HeaderButton = [&](const std::string &Label, bool Active, float MinWidth = 40.0f) {
			const float W = std::max(MinWidth, ui::Measure(pPainter, m_pGraphics->Font(), 12.0f, Label) + 14.0f);
			const base::rect Box = base::rect::FromSize(base::vec2(ButtonX, y), base::vec2(W, 22.0f));
			ButtonX += W + 4.0f;
			if(ButtonX > Area.m_BottomRight.x)
				return false; // a header that has run out of dock: no button
			if(ui::Hovered(Input, Box))
				Hint(Box, Label,  Label == Tr("New") ? Tr("add an envelope and hand it to the selected layer or quad") :
					Tr("set how many channels the envelope has, which is what its curves drive") );
			return ui::Button(pPainter, Input, m_pGraphics->Font(), Box, Label, Active);
		};

		if(HeaderButton("New", false, 48.0f))
		{
			if(EnsureEnvelopeSelected() < 0)
				SetStatus( Tr("select a layer or a quad first: an envelope needs an owner") );
			else
			{
				m_SelectedEnvelopePoint = -1;
				g_Envelope.m_ListScroll = 0.0f;
				g_Envelope.m_bZoomSet = false;
			}
		}
		for(int Channels = 1; Channels <= 4; Channels++)
		{
			const bool bClicked = HeaderButton(std::to_string(Channels), ChannelCount == Channels, 26.0f);
			if(!bClicked || pEnvelope == nullptr || pEnvelope->m_Channels == Channels)
				continue;
			// how many channels an envelope has is a property of the map, so
			// this is an edit and not a view setting
			pEnvelope->m_Channels = Channels;
			if(m_EnvelopeChannelBase >= Channels)
				m_EnvelopeChannelBase = 0;
			m_Dirty = true;
			Notify(  base::FormatRuntime(Tr("the envelope now has {:d} channels"), Channels)  );
		}
		y += 26.0f;

		if(pEnvelope == nullptr)
		{
			ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(X, y + 12.0f), 12.0f, ui::Theme.m_TextDim, Tr("no envelope is selected: pick a layer or quad with one, or press New") , Width);
			return;
		}

		// the length the plot covers: the last point, or a second when the
		// envelope is empty. Never zero, so nothing below divides by it.
		const float Loop = std::max(EnvelopeLength(*pEnvelope), 1.0f);
		const int PointCount = static_cast<int>(pEnvelope->m_vPoints.size());
		if(m_SelectedEnvelopePoint >= PointCount)
			m_SelectedEnvelopePoint = PointCount - 1;

		// -------------------------------------------------------- curve view
		const float FooterHeight = 16.0f;
		const float TransportHeight = 22.0f;
		const float Remaining = Area.m_BottomRight.y - y - FooterHeight - TransportHeight - 8.0f;
		const float ListHeight = base::clamp(Remaining * 0.4f, 44.0f, 200.0f);
		const float PlotHeight = Remaining - ListHeight - 4.0f;
		if(PlotHeight < 40.0f)
		{
			ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(X, y + 12.0f), 12.0f, ui::Theme.m_TextDim, Tr("make this dock taller to see the curve") , Width);
			return;
		}

		/*
			The y range of the plot, in fixed point units, is fitted to the
			envelope being shown. A fixed range cannot work: a colour envelope
			lives in 0..1024 and a quad position envelope reaches tens of
			thousands, so one range either flattens the colours into a single
			line or pushes the positions off the top. The span is snapped up to a
			power of two and centred on the data, which reads as round grid lines
			and stays stable while a point is dragged.
		*/
		constexpr float EnvelopeMinSpan = 256.0f; // a quarter of 1.0: never degenerate
		float EnvelopeValueMin = 0.0f;
		float EnvelopeValueMax = 1024.0f;
		if(pEnvelope != nullptr && !pEnvelope->m_vPoints.empty())
		{
			float Low = 0.0f, High = 0.0f;
			bool bAny = false;
			for(const CEnvelopePoint &Point : pEnvelope->m_vPoints)
			{
				for(int c = 0; c < 2; c++)
				{
					const int Channel = m_EnvelopeChannelBase + c;
					if(Channel < 0 || Channel >= pEnvelope->m_Channels)
						continue;
					const float Value = static_cast<float>(Point.m_aValues[Channel]);
					Low = bAny ? std::min(Low, Value) : Value;
					High = bAny ? std::max(High, Value) : Value;
					bAny = true;
				}
			}
			if(bAny)
			{
				const float Span = std::max(High - Low, 1.0f);
				float Step = EnvelopeMinSpan;
				while(Step < Span * 1.25f && Step < 16777216.0f)
					Step *= 2.0f;
				const float Middle = (Low + High) * 0.5f;
				EnvelopeValueMin = Middle - Step * 0.5f;
				EnvelopeValueMax = Middle + Step * 0.5f;
			}
		}

		const base::rect Plot = base::rect::FromSize(base::vec2(X, y), base::vec2(Width, PlotHeight));
		ui::Rect(pPainter, Plot, ui::Theme.m_PanelDark);
		ui::RectOutline(pPainter, Plot, ui::Theme.m_Border);

		// --------------------------------------------------- time axis and zoom
		/*
			m_EnvelopeZoom is a float in the editor and this view wants whole
			pixels per second: a zoom of 0.4 pixels per second would put the
			whole envelope inside a pixel. Rounding it on the way in keeps the
			value the editor stores honest and the arithmetic below exact.
		*/
		int Zoom = base::clamp(static_cast<int>(std::lround(m_EnvelopeZoom)), static_cast<int>(EnvelopeMinZoom),
			static_cast<int>(EnvelopeMaxZoom));
		if(!g_Envelope.m_bZoomSet)
		{
			EnvelopeZoomToShow(Loop, Plot.Width(), Zoom, m_EnvelopeScroll);
			g_Envelope.m_bZoomSet = true;
		}
		m_EnvelopeZoom = static_cast<float>(Zoom);

		const auto FixScroll = [&](float VisibleTime) {
			m_EnvelopeScroll = base::clamp(m_EnvelopeScroll, 0.0f, std::max(0.0f, Loop - VisibleTime));
		};
		FixScroll(Plot.Width() / static_cast<float>(Zoom));

		const auto TimeToX = [&](float Time) { return Plot.m_TopLeft.x + (Time - m_EnvelopeScroll) * static_cast<float>(Zoom); };
		const auto XToTime = [&](float ScreenX) { return m_EnvelopeScroll + (ScreenX - Plot.m_TopLeft.x) / static_cast<float>(Zoom); };
		const auto ValueToY = [&](float Value) {
			const float Span = EnvelopeValueMax - EnvelopeValueMin;
			const float Fraction = base::clamp((Value - EnvelopeValueMin) / Span, 0.0f, 1.0f);
			return Plot.m_BottomRight.y - Fraction * Plot.Height();
		};
		const auto YToValue = [&](float ScreenY) {
			const float Span = EnvelopeValueMax - EnvelopeValueMin;
			const float Fraction = base::clamp((Plot.m_BottomRight.y - ScreenY) / std::max(Plot.Height(), 1.0f), 0.0f, 1.0f);
			return EnvelopeValueMin + Fraction * Span;
		};
		// the handle of a point, at the value of the channel the view plots
		const auto HandleAt = [&](const CEnvelopePoint &Point) {
			const base::vec2 Center(TimeToX(static_cast<float>(Point.m_Time) / 1000.0f),
				ValueToY(static_cast<float>(Point.m_aValues[m_EnvelopeChannelBase])));
			return base::rect::FromSize(Center - base::vec2(EnvelopeHandleSize, EnvelopeHandleSize) * 0.5f,
				base::vec2(EnvelopeHandleSize, EnvelopeHandleSize));
		};

		pPainter->Save();
		pPainter->Scissor(Plot.m_TopLeft.x, Plot.m_TopLeft.y, Plot.Width(), Plot.Height());

		// The horizontal grid: zero when it is in view, then every power of two
		// of the value unit. The 22.10 unit is 1024, so "1.0" is a value of
		// 1024 and the labels say which multiple of it a line is.
		{
			std::vector<float> Values;
			Values.push_back(0.0f);
			for(int Power = 1; Power <= 24; Power++)
			{
				const float Value = std::pow(2.0f, static_cast<float>(Power));
				if(Value > EnvelopeValueMax)
					break;
				if(Value >= EnvelopeValueMin)
					Values.push_back(Value);
			}
			for(const float Value : Values)
			{
				const float GridY = ValueToY(Value);
				if(GridY < Plot.m_TopLeft.y - 1.0f || GridY > Plot.m_BottomRight.y + 1.0f)
					continue;
				if(g_Envelope.m_bGrid)
					ui::Line(pPainter, base::vec2(Plot.m_TopLeft.x, GridY), base::vec2(Plot.m_BottomRight.x, GridY), ui::Theme.m_CurveGrid);
				const std::string Label = Value < 1024.0f ? base::Format("{:.0f}", Value) : base::Format("x{:.0f}", Value / 1024.0f);
				ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Plot.m_TopLeft.x + 4.0f, GridY - 3.0f), 10.0f,
					ui::Theme.m_TextDim, Label);
			}
		}

		// the vertical grid, at a step that stays readable at every zoom
		const float Visible = Plot.Width() / static_cast<float>(Zoom);
		const float Step = std::pow(2.0f, std::floor(std::log2(std::max(Visible / 8.0f, 0.01f))));
		const int FirstGrid = static_cast<int>(std::floor(m_EnvelopeScroll / Step));
		const int LastGrid = static_cast<int>(std::ceil((m_EnvelopeScroll + Visible) / Step));
		if(g_Envelope.m_bGrid && LastGrid - FirstGrid <= 200)
		{
			for(int i = FirstGrid; i <= LastGrid; i++)
			{
				const float Time = static_cast<float>(i) * Step;
				if(Time < 0.0f)
					continue;
				const float GridX = TimeToX(Time);
				ui::Line(pPainter, base::vec2(GridX, Plot.m_TopLeft.y), base::vec2(GridX, Plot.m_BottomRight.y), ui::Theme.m_CurveGrid);
				ui::Text(pPainter, m_pGraphics->Font(), base::vec2(GridX + 3.0f, Plot.m_BottomRight.y - 4.0f), 10.0f,
					ui::Theme.m_TextDim,   base::FormatRuntime(Tr("{:.3g}s"), Time)  );
			}
		}

		// the two plotted channels. The first is the thick one: on a colour
		// envelope the pair is red and green, and red leads.
		const int ChannelsToPlot = std::min(2, std::max(0, ChannelCount - m_EnvelopeChannelBase));
		const int Samples = EnvelopeSamples(Plot.Width());
		const ui::CColor aCurveColor[2] = {ui::Theme.m_ChannelA, ui::Theme.m_ChannelB};
		for(int Pair = 0; Pair < ChannelsToPlot; Pair++)
		{
			const int Channel = m_EnvelopeChannelBase + Pair;
			pPainter->BeginPath();
			for(int s = 0; s <= Samples; s++)
			{
				const float Time = base::lerp(m_EnvelopeScroll, m_EnvelopeScroll + Visible, static_cast<float>(s) / Samples);
				CEnvelopeSample Sample;
				// the sample is taken at Time, so the trace shows the curve the
				// client will play, wrap at the last point included
				EvaluateEnvelope(*pEnvelope, std::max(Time, 0.0f), Sample, Channel + 1);
				const float ScreenX = TimeToX(Time);
				const float ScreenY = ValueToY(Sample[static_cast<std::size_t>(Channel)]);
				if(s == 0)
					pPainter->MoveTo(ScreenX, ScreenY);
				else
					pPainter->LineTo(ScreenX, ScreenY);
			}
			pPainter->StrokeColor(aCurveColor[Pair]);
			pPainter->StrokeWidth(Pair == 0 ? 2.0f : 1.25f);
			pPainter->Stroke();
		}

		// what the pair is worth at the preview clock, so the trace is readable
		// without counting grid lines
		if(ChannelsToPlot > 0)
		{
			CEnvelopeSample Sample;
			EvaluateEnvelope(*pEnvelope, std::max(m_EnvelopeTime, 0.0f), Sample, ChannelCount);
			for(int Pair = 0; Pair < ChannelsToPlot; Pair++)
			{
				const int Channel = m_EnvelopeChannelBase + Pair;
				ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Plot.m_TopLeft.x + 4.0f, Plot.m_TopLeft.y + 12.0f + Pair * 12.0f), 10.5f,
					aCurveColor[Pair],   base::FormatRuntime(Tr("ch {:d}  {:.3f}"), Channel, Sample[static_cast<std::size_t>(Channel)])  );
			}
		}

		// the point handles, drawn backwards so that the selected one ends on
		// top of its neighbours
		for(int i = PointCount - 1; i >= 0; i--)
		{
			if(i == m_SelectedEnvelopePoint)
				continue;
			const base::rect Handle = HandleAt(pEnvelope->m_vPoints[static_cast<std::size_t>(i)]);
			// off the visible time range: a handle clamped to the edge would
			// claim a time the point does not have
			if(Handle.m_BottomRight.x < Plot.m_TopLeft.x || Handle.m_TopLeft.x > Plot.m_BottomRight.x)
				continue;
			ui::Rect(pPainter, Handle, ui::Theme.m_Curve);
			ui::RectOutline(pPainter, Handle, ui::RGBA(20, 22, 26, 220), 1.5f);
		}
		if(m_SelectedEnvelopePoint >= 0 && m_SelectedEnvelopePoint < PointCount)
		{
			const base::rect Handle = HandleAt(pEnvelope->m_vPoints[static_cast<std::size_t>(m_SelectedEnvelopePoint)]);
			ui::Rect(pPainter, Handle, ui::Theme.m_Curve);
			ui::RectOutline(pPainter, Handle, ui::Theme.m_Accent, 2.5f);
		}

		// the time cursor of the preview clock
		const float CursorX = TimeToX(m_EnvelopeTime);
		if(CursorX >= Plot.m_TopLeft.x && CursorX <= Plot.m_BottomRight.x)
		{
			ui::Line(pPainter, base::vec2(CursorX, Plot.m_TopLeft.y), base::vec2(CursorX, Plot.m_BottomRight.y), ui::Theme.m_Accent);
			ui::Rect(pPainter, base::rect::FromSize(base::vec2(CursorX - 3.0f, Plot.m_TopLeft.y), base::vec2(6.0f, 3.0f)), ui::Theme.m_Accent);
		}

		if(PointCount == 0)
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Plot.Center().x, Plot.Center().y), 12.0f, ui::Theme.m_TextDim, Tr("no points yet: click the plot, or control-click a time") , ui::text_align::center);

		pPainter->Restore();

		// -------------------------------------------------------- curve input
		const bool bInPlot = ui::Hovered(Input, Plot);
		const bool Ctrl = Input.ModifierDown(engine::CInput::mod_ctrl);

		// the wheel zooms the time axis around the cursor, so the time that was
		// under the pointer stays under it
		if(bInPlot && Input.Scroll() != 0.0f)
		{
			const float Anchor = XToTime(Input.MousePosition().x);
			const float Wanted = static_cast<float>(Zoom) * std::pow(1.25f, Input.Scroll());
			const int NewZoom = base::clamp(static_cast<int>(std::lround(Wanted)), static_cast<int>(EnvelopeMinZoom),
				static_cast<int>(EnvelopeMaxZoom));
			if(NewZoom != Zoom)
			{
				// the anchor is kept in seconds, so it survives the change
				m_EnvelopeScroll = Anchor - (Input.MousePosition().x - Plot.m_TopLeft.x) / static_cast<float>(NewZoom);
				Zoom = NewZoom;
				m_EnvelopeZoom = static_cast<float>(NewZoom);
				g_Envelope.m_bZoomSet = true;
			}
		}
		FixScroll(Plot.Width() / static_cast<float>(Zoom));

		// the handle nearest the cursor, or -1 when none is within grabbing
		const auto HitHandle = [&]() {
			if(!bInPlot)
				return -1;
			int Best = -1;
			float BestDistance = EnvelopeHandleGrab;
			for(int i = 0; i < PointCount; i++)
			{
				const base::rect Handle = HandleAt(pEnvelope->m_vPoints[static_cast<std::size_t>(i)]);
				const base::vec2 Distance = Input.MousePosition() - Handle.Center();
				const float DistanceX = std::max(std::abs(Distance.x) - Handle.Width() * 0.5f, 0.0f);
				const float DistanceY = std::max(std::abs(Distance.y) - Handle.Height() * 0.5f, 0.0f);
				const float Length = std::sqrt(DistanceX * DistanceX + DistanceY * DistanceY);
				if(Length <= BestDistance)
				{
					BestDistance = Length;
					Best = i;
				}
			}
			return Best;
		};

		if(m_bEnvelopeDragging)
		{
			if(!Input.MouseDown(engine::CInput::mouse_button::left))
			{
				m_bEnvelopeDragging = false;
				g_EnvelopeDrag.m_Point = -1;
			}
			else if(g_EnvelopeDrag.m_Point >= 0 && g_EnvelopeDrag.m_Point < PointCount)
			{
				const base::vec2 Moved = Input.MousePosition() - g_EnvelopeDrag.m_Grab;
				if(!g_EnvelopeDrag.m_bMoved && std::abs(Moved.x) + std::abs(Moved.y) <= 2.0f)
				{
					// the dead zone of a click that has not turned into a drag
				}
				else
				{
					if(!g_EnvelopeDrag.m_bPushed)
					{
						// one snapshot for the whole drag, taken on the frame it
						// actually started to move the point
						PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::envelope);
						g_EnvelopeDrag.m_bPushed = true;
					}
					g_EnvelopeDrag.m_bMoved = true;
					// the setter clamps a point between its neighbours, so a
					// drag cannot reorder the envelope
					SetEnvelopePointTime(g_EnvelopeDrag.m_Point, ClampToInt(XToTime(Input.MousePosition().x) * 1000.0f));
					SetEnvelopePointValue(g_EnvelopeDrag.m_Point, m_EnvelopeChannelBase, ClampToInt(YToValue(Input.MousePosition().y)));
				}
			}
		}
		else if(bInPlot && Input.MousePressed(engine::CInput::mouse_button::left))
		{
			const int Hit = HitHandle();
			if(Hit >= 0)
			{
				m_SelectedEnvelopePoint = Hit;
				m_bEnvelopeDragging = true;
				g_EnvelopeDrag = CEnvelopeDrag{};
				g_EnvelopeDrag.m_Point = Hit;
				g_EnvelopeDrag.m_Grab = Input.MousePosition();
			}
			else if(Ctrl || PointCount == 0)
			{
				// an empty envelope has nothing to select, so a plain click
				// already means "a point here"
				PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::envelope);
				AddEnvelopePoint(std::max(0.0f, XToTime(Input.MousePosition().x)));
			}
			else
				m_SelectedEnvelopePoint = -1;
		}

		if(bInPlot && Input.MousePressed(engine::CInput::mouse_button::right))
		{
			// a right click deletes the point under it, which the handle hit
			// test already answers
			const int Hit = HitHandle();
			if(Hit >= 0)
			{
				PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::envelope);
				DeleteEnvelopePoint(Hit);
			}
		}

		y = Plot.m_BottomRight.y + 4.0f;

		// ------------------------------------------------------- the point list
		const float ListHeightActual = std::max(Area.m_BottomRight.y - y - FooterHeight - TransportHeight - 4.0f, 30.0f);
		const base::rect List = base::rect::FromSize(base::vec2(X, y), base::vec2(Width, ListHeightActual));
		ui::Rect(pPainter, List, ui::Theme.m_PanelDark);
		ui::RectOutline(pPainter, List, ui::Theme.m_Border);
		ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(List.m_TopLeft.x + 6.0f, List.m_TopLeft.y + 12.0f), 11.0f,
			ui::Theme.m_TextDim,   base::FormatRuntime(Tr("{:d} points"), PointCount)  , List.Width() - 12.0f);

		pPainter->Save();
		pPainter->Scissor(List.m_TopLeft.x, List.m_TopLeft.y + 18.0f, List.Width(), std::max(1.0f, List.Height() - 18.0f));
		float ry = List.m_TopLeft.y + 19.0f - g_Envelope.m_ListScroll;
		for(int i = 0; i < PointCount; i++)
		{
			const CEnvelopePoint &Point = pEnvelope->m_vPoints[static_cast<std::size_t>(i)];
			const base::rect Row = base::rect::FromSize(base::vec2(List.m_TopLeft.x + 2.0f, ry), base::vec2(List.Width() - 4.0f, EnvelopeRowHeight));
			ry += EnvelopeRowHeight;
			if(Row.m_BottomRight.y < List.m_TopLeft.y + 18.0f || Row.m_TopLeft.y > List.m_BottomRight.y)
				continue;
			if(i == m_SelectedEnvelopePoint)
				ui::Rect(pPainter, Row, ui::Theme.m_Selection);
			else if(ui::Hovered(Input, Row))
				ui::Rect(pPainter, Row, ui::Theme.m_Hover);
			ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Row.m_TopLeft.x + 4.0f, Row.Center().y + 4.0f), 11.0f,
				ui::Theme.m_Text,   base::FormatRuntime(Tr("{:3d}  {:6d}ms  {:<7}  {:5d} {:5d} {:5d} {:5d}"), i, Point.m_Time,
					CurveName(Point.m_Curvetype).c_str(), Point.m_aValues[0], Point.m_aValues[1], Point.m_aValues[2], Point.m_aValues[3])  ,
				Row.Width() - 8.0f);
			if(ui::Hovered(Input, Row) && Input.MousePressed(engine::CInput::mouse_button::left))
				m_SelectedEnvelopePoint = i;
		}
		pPainter->Restore();

		// the list has its own scroll: the wheel over the curve is the zoom
		if(!bInPlot && ui::Hovered(Input, List) && Input.Scroll() != 0.0f)
			g_Envelope.m_ListScroll = std::max(0.0f, g_Envelope.m_ListScroll - Input.Scroll() * EnvelopeRowHeight * 3.0f);
		{
			const float Content = static_cast<float>(PointCount) * EnvelopeRowHeight;
			const float View = std::max(1.0f, List.Height() - 19.0f);
			g_Envelope.m_ListScroll = Content > View ? base::clamp(g_Envelope.m_ListScroll, 0.0f, Content - View) : 0.0f;
		}
		y = List.m_BottomRight.y + 4.0f;

		// ---------------------------------------- the selected point's curve type
		const bool bHasPoint = m_SelectedEnvelopePoint >= 0 && m_SelectedEnvelopePoint < PointCount;
		if(bHasPoint && !m_bEnvelopeDragging)
		{
			CEnvelopePoint &Point = pEnvelope->m_vPoints[static_cast<std::size_t>(m_SelectedEnvelopePoint)];
			const base::rect CurveButton = base::rect::FromSize(base::vec2(X, y), base::vec2(76.0f, 18.0f));
			if(ui::Hovered(Input, CurveButton))
				Hint(CurveButton, CurveName(Point.m_Curvetype), Tr("the interpolation of this keyframe: click to step through the curve types") );
			if(ui::Button(pPainter, Input, m_pGraphics->Font(), CurveButton, CurveName(Point.m_Curvetype)))
			{
				PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::envelope);
				Point.m_Curvetype = (Point.m_Curvetype + 1) % NUM_CURVETYPES;
				m_Dirty = true;
				Notify(  base::FormatRuntime(Tr("point {:d} is now '{}'"), m_SelectedEnvelopePoint, CurveName(Point.m_Curvetype).c_str())  );
			}
			/*
				The tangents of a bezier point are packed into the point itself,
				the way Teeworlds stores them, and this workspace does not expose
				them yet. Saying so is better than a button that looks like it
				edits them.
			*/
			const float HintWidth = std::max(Width - 190.0f, 20.0f);
			if(Point.m_Curvetype == CURVETYPE_BEZIER)
				ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(X + 82.0f, y + 12.0f), 10.5f, ui::Theme.m_TextDim, Tr("bezier tangents are not editable here yet") , HintWidth);
			else
				ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(X + 82.0f, y + 12.0f), 10.5f, ui::Theme.m_TextDim,  
					base::FormatRuntime(Tr("{:.3f}s"), static_cast<float>(Point.m_Time) / 1000.0f)  , HintWidth);

			const base::rect DeleteButton = base::rect::FromSize(base::vec2(Area.m_BottomRight.x - 88.0f, y),
				base::vec2(80.0f, 18.0f));
			if(ui::Hovered(Input, DeleteButton))
				Hint(DeleteButton, Tr("Delete point") , Tr("remove the selected keyframe, one step of undo") );
			if(ui::Button(pPainter, Input, m_pGraphics->Font(), DeleteButton, Tr("Delete point") ))
			{
				PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::envelope);
				DeleteEnvelopePoint(m_SelectedEnvelopePoint);
			}
		}
		else
			ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(X, y + 12.0f), 11.0f, ui::Theme.m_TextDim, Tr("click a point of the list, or a handle in the curve, to edit it") , Width);
		y += 22.0f;

		// -------------------------------------------------------- transport bar
		// the same clock the layer panel shows, so the preview the curve is
		// edited against is the one the map plays
		DrawEnvelopeBar(pPainter, base::rect::FromSize(base::vec2(X, y), base::vec2(Width, TransportHeight)));
		y += TransportHeight;

		// ------------------------------------------------------------- footnote
		ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(X, y + 12.0f), 10.5f, ui::Theme.m_TextDim, 
			base::FormatRuntime(Tr("wheel over the curve: zoom   ctrl+click: add   right click: delete   shift+{:c}: channel pair   {:c}: grid"),
				static_cast<char>(EnvelopeKeyChannel), static_cast<char>(EnvelopeKeyGrid)) , Width);

		// ------------------------------------------------------------- shortcuts
		/*
			They only mean this while the workspace is showing. [ ] already mean
			the brush size and - = the preview speed, so this uses two keys that
			nothing else on a layer wants.
		*/
		const bool Shift = Input.KeyDown(engine::keys::LShift) || Input.KeyDown(engine::keys::RShift);
		if(Shift && Input.KeyPressed(EnvelopeKeyChannel))
		{
			m_EnvelopeChannelBase = m_EnvelopeChannelBase == 0 ? 2 : 0;
			if(m_EnvelopeChannelBase >= ChannelCount)
				m_EnvelopeChannelBase = 0;
			Notify(  base::FormatRuntime(Tr("the curve view now plots channel {:d}"), m_EnvelopeChannelBase)  );
		}
		if(!Shift && Input.KeyPressed(EnvelopeKeyGrid))
		{
			g_Envelope.m_bGrid = !g_Envelope.m_bGrid;
			SetStatus(  g_Envelope.m_bGrid ? Tr("envelope grid on") : Tr("envelope grid off")  );
		}
	}
} // namespace game
