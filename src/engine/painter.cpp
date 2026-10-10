#include "painter.h"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>

#include "base/logger.h"

namespace engine::painter
{
	namespace
	{
		constexpr float Pi = 3.14159265358979323846f;
		// how many segments a circle is approximated with, per unit of radius
		constexpr int CircleSegments = 32;

		float Cross(const CPainter::CPoint &O, const CPainter::CPoint &A, const CPainter::CPoint &B)
		{
			return (A.m_X - O.m_X) * (B.m_Y - O.m_Y) - (A.m_Y - O.m_Y) * (B.m_X - O.m_X);
		}

		bool PointInTriangle(const CPainter::CPoint &P, const CPainter::CPoint &A,
			const CPainter::CPoint &B, const CPainter::CPoint &C)
		{
			const float D1 = Cross(A, B, P);
			const float D2 = Cross(B, C, P);
			const float D3 = Cross(C, A, P);
			const bool bHasNegative = D1 < 0.0f || D2 < 0.0f || D3 < 0.0f;
			const bool bHasPositive = D1 > 0.0f || D2 > 0.0f || D3 > 0.0f;
			return !(bHasNegative && bHasPositive);
		}

		float PolygonArea(const std::vector<CPainter::CPoint> &Points)
		{
			float Area = 0.0f;
			for(std::size_t i = 0; i < Points.size(); i++)
			{
				const std::size_t j = (i + 1) % Points.size();
				Area += Points[i].m_X * Points[j].m_Y - Points[j].m_X * Points[i].m_Y;
			}
			return Area * 0.5f;
		}
	} // namespace

	void CPainter::Init(SDL_Renderer *pRenderer, CTextEngine *pText)
	{
		m_pRenderer = pRenderer;
		m_pText = pText;
		Reset();
	}

	void CPainter::Close()
	{
		if(m_pGlyphTexture != nullptr)
		{
			SDL_DestroyTexture(m_pGlyphTexture);
			m_pGlyphTexture = nullptr;
		}
		m_pRenderer = nullptr;
		m_pText = nullptr;
	}

	void CPainter::BeginFrame(float Width, float Height)
	{
		(void)Width;
		(void)Height;
		Reset();
		m_UploadedAtlasRevision = 0;
	}

	void CPainter::EndFrame() {}

	void CPainter::Reset()
	{
		m_Stack.clear();
		m_State = CState{};
		m_Path.clear();
	}

	void CPainter::Save() { m_Stack.push_back(m_State); }

	void CPainter::Restore()
	{
		if(m_Stack.empty())
			return;
		m_State = m_Stack.back();
		m_Stack.pop_back();
	}

	void CPainter::Translate(float X, float Y)
	{
		m_State.m_TranslateX += X * m_State.m_ScaleX;
		m_State.m_TranslateY += Y * m_State.m_ScaleY;
	}

	void CPainter::Rotate(float Radians) { m_State.m_Rotation += Radians; }

	void CPainter::Scale(float X, float Y)
	{
		m_State.m_ScaleX *= X;
		m_State.m_ScaleY *= Y;
	}

	void CPainter::Scissor(float X, float Y, float Width, float Height)
	{
		const base::vec2 TopLeft = Transform(X, Y);
		// the transform can mirror, so the far corner is measured the same way
		const base::vec2 BottomRight = Transform(X + Width, Y + Height);
		const base::vec2 Minimum(std::min(TopLeft.x, BottomRight.x), std::min(TopLeft.y, BottomRight.y));
		const base::vec2 Maximum(std::max(TopLeft.x, BottomRight.x), std::max(TopLeft.y, BottomRight.y));
		base::rect Rect(Minimum, Maximum);
		if(m_State.m_bScissor)
			Rect = Rect.Intersect(m_State.m_Scissor);
		m_State.m_bScissor = Rect.Width() > 0.0f && Rect.Height() > 0.0f;
		m_State.m_Scissor = Rect;
	}

	void CPainter::ResetScissor() { m_State.m_bScissor = false; }

	base::vec2 CPainter::Transform(float X, float Y) const
	{
		float Sx = X * m_State.m_ScaleX;
		float Sy = Y * m_State.m_ScaleY;
		if(m_State.m_Rotation != 0.0f)
		{
			const float Cos = std::cos(m_State.m_Rotation);
			const float Sin = std::sin(m_State.m_Rotation);
			const float Rx = Sx * Cos - Sy * Sin;
			const float Ry = Sx * Sin + Sy * Cos;
			Sx = Rx;
			Sy = Ry;
		}
		return base::vec2(Sx + m_State.m_TranslateX, Sy + m_State.m_TranslateY);
	}

	bool CPainter::ClipRect(base::rect &Rect) const
	{
		if(m_State.m_bScissor)
		{
			Rect = Rect.Intersect(m_State.m_Scissor);
			if(Rect.Width() <= 0.0f || Rect.Height() <= 0.0f)
				return false;
		}
		return true;
	}

	void CPainter::BeginPath()
	{
		m_Path.clear();
		m_PathStart = CPoint{0.0f, 0.0f};
		m_bPathClosed = false;
	}

	void CPainter::MoveTo(float X, float Y)
	{
		const base::vec2 P = Transform(X, Y);
		m_Path.push_back(CPoint{P.x, P.y, X, Y});
		m_PathStart = CPoint{P.x, P.y, X, Y};
	}

	void CPainter::LineTo(float X, float Y)
	{
		const base::vec2 P = Transform(X, Y);
		m_Path.push_back(CPoint{P.x, P.y, X, Y});
	}

	void CPainter::ClosePath() { m_bPathClosed = true; }

	void CPainter::Rect(float X, float Y, float Width, float Height)
	{
		const base::vec2 A = Transform(X, Y);
		const base::vec2 B = Transform(X + Width, Y);
		const base::vec2 C = Transform(X + Width, Y + Height);
		const base::vec2 D = Transform(X, Y + Height);
		m_Path.push_back(CPoint{A.x, A.y, X, Y});
		m_Path.push_back(CPoint{B.x, B.y, X + Width, Y});
		m_Path.push_back(CPoint{C.x, C.y, X + Width, Y + Height});
		m_Path.push_back(CPoint{D.x, D.y, X, Y + Height});
		m_PathStart = CPoint{A.x, A.y, X, Y};
	}

	void CPainter::RoundedRect(float X, float Y, float Width, float Height, float Radius)
	{
		const float R = std::min(Radius, std::min(Width, Height) * 0.5f);
		if(R <= 0.0f)
		{
			Rect(X, Y, Width, Height);
			return;
		}
		const int Steps = std::max(2, static_cast<int>(CircleSegments / 4));
		const float Corners[4][2] = {
			{X + Width - R, Y + R}, {X + Width - R, Y + Height - R},
			{X + R, Y + Height - R}, {X + R, Y + R}};
		const float Start[4] = {-Pi * 0.5f, 0.0f, Pi * 0.5f, Pi};
		for(int c = 0; c < 4; c++)
		{
			for(int s = 0; s <= Steps; s++)
			{
				const float Angle = Start[c] + (Pi * 0.5f) * static_cast<float>(s) / Steps;
				const float Px = Corners[c][0] + std::cos(Angle) * R;
				const float Py = Corners[c][1] + std::sin(Angle) * R;
				const base::vec2 P = Transform(Px, Py);
				m_Path.push_back(CPoint{P.x, P.y, Px, Py});
			}
		}
		m_PathStart = m_Path.front();
	}

	void CPainter::Circle(float X, float Y, float Radius)
	{
		const int Steps = std::max(8, CircleSegments);
		for(int i = 0; i < Steps; i++)
		{
			const float Angle = 2.0f * Pi * static_cast<float>(i) / Steps;
			const float Cx = X + std::cos(Angle) * Radius;
			const float Cy = Y + std::sin(Angle) * Radius;
			const base::vec2 P = Transform(Cx, Cy);
			m_Path.push_back(CPoint{P.x, P.y, Cx, Cy});
		}
		if(!m_Path.empty())
			m_PathStart = m_Path.front();
	}

	void CPainter::PathWinding(winding Winding) { m_Winding = Winding; }

	void CPainter::FillColor(const CColor &Color)
	{
		m_FillColor = Color;
		m_bFillIsPaint = false;
	}

	void CPainter::FillPaint(const CPaint &Paint)
	{
		m_FillPaint = Paint;
		m_bFillIsPaint = true;
	}

	// ------------------------------------------------------------------ emit

	void CPainter::EmitTriangles(const std::vector<CVertex> &Vertices, SDL_Texture *pTexture, bool bTextured)
	{
		if(Vertices.empty())
			return;
		SDL_SetRenderDrawBlendMode(m_pRenderer, SDL_BLENDMODE_BLEND);
		if(pTexture != nullptr)
			SDL_SetTextureBlendMode(pTexture, SDL_BLENDMODE_BLEND);

		// SDL takes an interleaved vertex array; the painter's layout is the
		// same, so the vector can be handed over directly
		SDL_RenderGeometryRaw(m_pRenderer, pTexture,
			&Vertices[0].m_X, sizeof(CVertex),
			reinterpret_cast<const SDL_FColor *>(&Vertices[0].m_R), sizeof(CVertex),
			&Vertices[0].m_U, sizeof(CVertex),
			static_cast<int>(Vertices.size()), nullptr, 0, 0);
		(void)bTextured;
	}

	void CPainter::FillPolygon(const std::vector<CPoint> &Points, const CColor &Color)
	{
		if(Points.size() < 3 || Color.a <= 0.0f)
			return;

		std::vector<CVertex> Vertices;
		Vertices.reserve(Points.size() * 3);

		if(Points.size() == 3)
		{
			for(const CPoint &P : Points)
				Vertices.push_back(CVertex{P.m_X, P.m_Y, 0.0f, 0.0f, Color.r, Color.g, Color.b, Color.a});
		}
		else
		{
			// ear clipping: find a convex corner whose triangle is empty and
			// cut it off until three points are left
			std::vector<int> Remaining(Points.size());
			for(std::size_t i = 0; i < Points.size(); i++)
				Remaining[i] = static_cast<int>(i);
			// the polygon may be given either way round, so the convexity test
			// follows the actual orientation
			const bool bClockwise = PolygonArea(Points) < 0.0f;

			int Guard = 0;
			const int MaxIterations = static_cast<int>(Points.size()) * static_cast<int>(Points.size()) + 16;
			while(Remaining.size() > 3 && Guard++ < MaxIterations)
			{
				bool bCut = false;
				for(std::size_t i = 0; i < Remaining.size(); i++)
				{
					const int Ia = Remaining[(i + Remaining.size() - 1) % Remaining.size()];
					const int Ib = Remaining[i];
					const int Ic = Remaining[(i + 1) % Remaining.size()];
					const CPoint &A = Points[static_cast<std::size_t>(Ia)];
					const CPoint &B = Points[static_cast<std::size_t>(Ib)];
					const CPoint &C = Points[static_cast<std::size_t>(Ic)];
					const float Turn = Cross(A, B, C);
					if(bClockwise ? (Turn > 0.0f) : (Turn < 0.0f))
						continue;

					bool bEmpty = true;
					for(const int Index : Remaining)
					{
						if(Index == Ia || Index == Ib || Index == Ic)
							continue;
						if(PointInTriangle(Points[static_cast<std::size_t>(Index)], A, B, C))
						{
							bEmpty = false;
							break;
						}
					}
					if(!bEmpty)
						continue;

					Vertices.push_back(CVertex{A.m_X, A.m_Y, 0.0f, 0.0f, Color.r, Color.g, Color.b, Color.a});
					Vertices.push_back(CVertex{B.m_X, B.m_Y, 0.0f, 0.0f, Color.r, Color.g, Color.b, Color.a});
					Vertices.push_back(CVertex{C.m_X, C.m_Y, 0.0f, 0.0f, Color.r, Color.g, Color.b, Color.a});
					Remaining.erase(Remaining.begin() + static_cast<std::ptrdiff_t>(i));
					bCut = true;
					break;
				}
				if(!bCut)
					break; // degenerate outline, fall back to a fan below
			}
			for(std::size_t i = 1; i + 1 < Remaining.size(); i++)
			{
				const CPoint &A = Points[static_cast<std::size_t>(Remaining[0])];
				const CPoint &B = Points[static_cast<std::size_t>(Remaining[i])];
				const CPoint &C = Points[static_cast<std::size_t>(Remaining[i + 1])];
				Vertices.push_back(CVertex{A.m_X, A.m_Y, 0.0f, 0.0f, Color.r, Color.g, Color.b, Color.a});
				Vertices.push_back(CVertex{B.m_X, B.m_Y, 0.0f, 0.0f, Color.r, Color.g, Color.b, Color.a});
				Vertices.push_back(CVertex{C.m_X, C.m_Y, 0.0f, 0.0f, Color.r, Color.g, Color.b, Color.a});
			}
		}
		EmitTriangles(Vertices, nullptr, false);
	}

	void CPainter::FillPolygonPaint(const std::vector<CPoint> &Points, const CPaint &Paint)
	{
		if(Points.size() < 3)
			return;

		SDL_Texture *pTexture = nullptr;
		if(Paint.m_Kind == CPaint::kind::image)
		{
			pTexture = LookupTexture(Paint.m_Texture);
			if(pTexture == nullptr)
				return;
		}

		// the shape is filled as a fan, which is what an axis aligned quad
		// (every image fill in the interface) needs; a fan is also what a
		// convex outlined region produces
		std::vector<CVertex> Vertices;
		Vertices.reserve((Points.size() - 2) * 3);

		for(std::size_t i = 1; i + 1 < Points.size(); i++)
		{
			const CPoint *paTriangle[3] = {&Points[0], &Points[i], &Points[i + 1]};
			for(const CPoint *pPoint : paTriangle)
			{
				CVertex V;
				V.m_X = pPoint->m_X;
				V.m_Y = pPoint->m_Y;
				switch(Paint.m_Kind)
				{
				case CPaint::kind::solid:
				{
					V.m_R = Paint.m_Color.r;
					V.m_G = Paint.m_Color.g;
					V.m_B = Paint.m_Color.b;
					V.m_A = Paint.m_Color.a;
					V.m_U = 0.0f;
					V.m_V = 0.0f;
					break;
				}
				case CPaint::kind::linear_gradient:
				{
					// project the point onto the gradient axis
					const float Dx = Paint.m_End.x - Paint.m_Start.x;
					const float Dy = Paint.m_End.y - Paint.m_Start.y;
					const float LengthSquared = Dx * Dx + Dy * Dy;
					float T = 0.0f;
					if(LengthSquared > 0.0001f)
						T = ((pPoint->m_X - Paint.m_Start.x) * Dx + (pPoint->m_Y - Paint.m_Start.y) * Dy) / LengthSquared;
					T = std::clamp(T, 0.0f, 1.0f);
					V.m_R = Paint.m_Inner.r + (Paint.m_Outer.r - Paint.m_Inner.r) * T;
					V.m_G = Paint.m_Inner.g + (Paint.m_Outer.g - Paint.m_Inner.g) * T;
					V.m_B = Paint.m_Inner.b + (Paint.m_Outer.b - Paint.m_Inner.b) * T;
					V.m_A = Paint.m_Inner.a + (Paint.m_Outer.a - Paint.m_Inner.a) * T;
					V.m_U = 0.0f;
					V.m_V = 0.0f;
					break;
				}
				case CPaint::kind::image:
				{
					// the image rectangle maps onto the paint's rectangle: the
					// position inside it selects a texel inside the paint's
					// texel window, which is how a tile atlas is sampled
					const float Width = Paint.m_ImageSize.x > 0.0f ? Paint.m_ImageSize.x : 1.0f;
					const float Height = Paint.m_ImageSize.y > 0.0f ? Paint.m_ImageSize.y : 1.0f;
					const float U = (pPoint->m_LocalX - Paint.m_ImageTopLeft.x) / Width;
					const float TexV = (pPoint->m_LocalY - Paint.m_ImageTopLeft.y) / Height;
					// the texel window is normalized by the source texture size:
					// the renderer samples in 0..1, not in texels
					const float SourceWidth = Paint.m_TexTextureWidth > 0 ? static_cast<float>(Paint.m_TexTextureWidth) : 1.0f;
					const float SourceHeight = Paint.m_TexTextureHeight > 0 ? static_cast<float>(Paint.m_TexTextureHeight) : 1.0f;
					V.m_U = (Paint.m_TexX + U * Paint.m_TexWidth) / SourceWidth;
					V.m_V = (Paint.m_TexY + TexV * Paint.m_TexHeight) / SourceHeight;
					V.m_R = 1.0f;
					V.m_G = 1.0f;
					V.m_B = 1.0f;
					V.m_A = Paint.m_Alpha;
					break;
				}
				}
				Vertices.push_back(V);
			}
		}
		EmitTriangles(Vertices, pTexture, true);
	}

	void CPainter::Fill()
	{
		if(m_Path.empty())
			return;

		std::vector<CPoint> Points = m_Path;
		// drop a duplicated closing point, which would make the outline
		// degenerate
		if(Points.size() > 1 && Points.front().m_X == Points.back().m_X && Points.front().m_Y == Points.back().m_Y)
			Points.pop_back();

		if(m_bFillIsPaint)
			FillPolygonPaint(Points, m_FillPaint);
		else
			FillPolygon(Points, m_FillColor);
	}

	void CPainter::EmitStroke(const std::vector<CPoint> &Points, bool bClosed, const CColor &Color, float Width)
	{
		if(Points.size() < 2 || Color.a <= 0.0f || Width <= 0.0f)
			return;
		const float Half = Width * 0.5f;
		std::vector<CVertex> Vertices;
		const std::size_t Count = bClosed ? Points.size() : Points.size() - 1;
		for(std::size_t i = 0; i < Count; i++)
		{
			const CPoint &A = Points[i];
			const CPoint &B = Points[(i + 1) % Points.size()];
			float Dx = B.m_X - A.m_X;
			float Dy = B.m_Y - A.m_Y;
			const float Length = std::sqrt(Dx * Dx + Dy * Dy);
			if(Length < 0.0001f)
				continue;
			Dx /= Length;
			Dy /= Length;
			// the segment is one quad, with the ends extended by half a width
			// so the corners meet without a notch
			const float Nx = -Dy * Half;
			const float Ny = Dx * Half;
			const float Ex = Dx * Half;
			const float Ey = Dy * Half;
			const float Ax = A.m_X - Ex, Ay = A.m_Y - Ey;
			const float Bx = B.m_X + Ex, By = B.m_Y + Ey;

			const CVertex V0{Ax + Nx, Ay + Ny, 0.0f, 0.0f, Color.r, Color.g, Color.b, Color.a};
			const CVertex V1{Bx + Nx, By + Ny, 0.0f, 0.0f, Color.r, Color.g, Color.b, Color.a};
			const CVertex V2{Bx - Nx, By - Ny, 0.0f, 0.0f, Color.r, Color.g, Color.b, Color.a};
			const CVertex V3{Ax - Nx, Ay - Ny, 0.0f, 0.0f, Color.r, Color.g, Color.b, Color.a};
			Vertices.push_back(V0);
			Vertices.push_back(V1);
			Vertices.push_back(V2);
			Vertices.push_back(V0);
			Vertices.push_back(V2);
			Vertices.push_back(V3);
		}
		EmitTriangles(Vertices, nullptr, false);
	}

	void CPainter::Stroke()
	{
		if(m_Path.size() < 2)
			return;
		// whether the outline is closed is what ClosePath() recorded, not a
		// guess from the point count: the caller knows
		EmitStroke(m_Path, m_bPathClosed, m_StrokeColor, m_StrokeWidth);
	}

	void CPainter::StrokeColor(const CColor &Color) { m_StrokeColor = Color; }
	void CPainter::StrokeWidth(float Width) { m_StrokeWidth = Width; }

	// -------------------------------------------------------------- shortcuts

	void CPainter::TexturedQuad(int Texture, const base::vec2 *paCorners, const base::vec2 *paTexcoords,
		const CColor *paColors, int Cells, float Alpha)
	{
		SDL_Texture *pTexture = LookupTexture(Texture);
		if(pTexture == nullptr || Alpha <= 0.0f)
			return;

		const int Steps = std::clamp(Cells, 1, 32);
		std::vector<CVertex> Vertices;
		Vertices.reserve(static_cast<std::size_t>(Steps) * Steps * 6);

		/*
			The surface is bilinear over the corners: 0 and 1 are the top edge,
			2 and 3 the bottom one, which is the layout the map stores a quad
			in and the one its texcoords and colours follow. Every corner of a
			cell is placed by (u,v), so the grid follows a quad that is not a
			rectangle at all.
		*/
		const auto Corner = [&](float u, float v, base::vec2 &Point, base::vec2 &Tex, CColor &Colour) {
			const base::vec2 TopPoint = paCorners[0] + (paCorners[1] - paCorners[0]) * u;
			const base::vec2 BottomPoint = paCorners[2] + (paCorners[3] - paCorners[2]) * u;
			Point = TopPoint + (BottomPoint - TopPoint) * v;

			const base::vec2 TopTex = paTexcoords[0] + (paTexcoords[1] - paTexcoords[0]) * u;
			const base::vec2 BottomTex = paTexcoords[2] + (paTexcoords[3] - paTexcoords[2]) * u;
			Tex = TopTex + (BottomTex - TopTex) * v;

			// the corner colours interpolate the same way
			CColor TopColour, BottomColour;
			const float aTop[4] = {paColors[0].r, paColors[0].g, paColors[0].b, paColors[0].a};
			const float aBottom[4] = {paColors[1].r, paColors[1].g, paColors[1].b, paColors[1].a};
			const float aLowTop[4] = {paColors[2].r, paColors[2].g, paColors[2].b, paColors[2].a};
			const float aLowBottom[4] = {paColors[3].r, paColors[3].g, paColors[3].b, paColors[3].a};
			float aTopResult[4], aBottomResult[4], aResult[4];
			for(int c = 0; c < 4; c++)
			{
				aTopResult[c] = base::lerp(aTop[c], aBottom[c], u);
				aBottomResult[c] = base::lerp(aLowTop[c], aLowBottom[c], u);
				aResult[c] = base::lerp(aTopResult[c], aBottomResult[c], v);
			}
			Colour = CColor{aResult[0], aResult[1], aResult[2], aResult[3] * Alpha};
		};

		for(int j = 0; j < Steps; j++)
		{
			for(int i = 0; i < Steps; i++)
			{
				const float u0 = static_cast<float>(i) / Steps;
				const float u1 = static_cast<float>(i + 1) / Steps;
				const float v0 = static_cast<float>(j) / Steps;
				const float v1 = static_cast<float>(j + 1) / Steps;

				// the cell corners, in the order top left, top right, bottom
				// left, bottom right, so the two triangles do not cross it
				const float aU[4] = {u0, u1, u0, u1};
				const float aV[4] = {v0, v0, v1, v1};
				CVertex aCorner[4];
				for(int c = 0; c < 4; c++)
				{
					base::vec2 Point, Tex;
					CColor Colour;
					Corner(aU[c], aV[c], Point, Tex, Colour);
					aCorner[c] = CVertex{Point.x, Point.y, Tex.x, Tex.y, Colour.r, Colour.g, Colour.b, Colour.a};
				}
				const int aOrder[6] = {0, 1, 3, 0, 3, 2};
				for(const int c : aOrder)
					Vertices.push_back(aCorner[c]);
			}
		}
		EmitTriangles(Vertices, pTexture, true);
	}

	void CPainter::FillRect(float X, float Y, float Width, float Height, const CColor &Color)
	{
		if(Color.a <= 0.0f)
			return;
		base::rect Rect(base::vec2(X, Y), base::vec2(X + Width, Y + Height));
		if(!ClipRect(Rect))
			return;

		// the rectangle is clipped, so the geometry needs no scissor
		std::vector<CPoint> Points = {
			CPoint{Rect.m_TopLeft.x, Rect.m_TopLeft.y, Rect.m_TopLeft.x, Rect.m_TopLeft.y},
			CPoint{Rect.m_BottomRight.x, Rect.m_TopLeft.y, Rect.m_BottomRight.x, Rect.m_TopLeft.y},
			CPoint{Rect.m_BottomRight.x, Rect.m_BottomRight.y, Rect.m_BottomRight.x, Rect.m_BottomRight.y},
			CPoint{Rect.m_TopLeft.x, Rect.m_BottomRight.y, Rect.m_TopLeft.x, Rect.m_BottomRight.y}};
		FillPolygon(Points, Color);
	}

	void CPainter::StrokeRect(float X, float Y, float Width, float Height, const CColor &Color, float Width2)
	{
		base::rect Rect(base::vec2(X, Y), base::vec2(X + Width, Y + Height));
		if(!ClipRect(Rect))
			return;
		std::vector<CPoint> Points = {
			CPoint{Rect.m_TopLeft.x, Rect.m_TopLeft.y, Rect.m_TopLeft.x, Rect.m_TopLeft.y},
			CPoint{Rect.m_BottomRight.x, Rect.m_TopLeft.y, Rect.m_BottomRight.x, Rect.m_TopLeft.y},
			CPoint{Rect.m_BottomRight.x, Rect.m_BottomRight.y, Rect.m_BottomRight.x, Rect.m_BottomRight.y},
			CPoint{Rect.m_TopLeft.x, Rect.m_BottomRight.y, Rect.m_TopLeft.x, Rect.m_BottomRight.y}};
		EmitStroke(Points, true, Color, Width2);
	}

	void CPainter::DrawLine(float X0, float Y0, float X1, float Y1, const CColor &Color, float Width2)
	{
		std::vector<CPoint> Points = {CPoint{X0, Y0}, CPoint{X1, Y1}};
		EmitStroke(Points, false, Color, Width2);
	}

	void CPainter::DrawCircle(float X, float Y, float Radius, const CColor &Color)
	{
		BeginPath();
		Circle(X, Y, Radius);
		FillColor(Color);
		Fill();
	}

	// ------------------------------------------------------------------ text

	void CPainter::FontFaceId(int Face) { m_Font = Face; }
	void CPainter::FontSize(float Size) { m_FontSize = Size; }
	void CPainter::TextAlign(int Align) { m_Align = Align; }

	float CPainter::LineHeight() const
	{
		return (m_pText != nullptr) ? m_pText->LineHeight(m_Font, m_FontSize) : m_FontSize * 1.2f;
	}

	float CPainter::TextBounds(float X, float Y, const std::string &Text)
	{
		(void)X;
		(void)Y;
		if(m_pText == nullptr)
			return 0.0f;
		// shaping also rasterizes, so the measurement costs the same as a draw
		m_GlyphQuads.clear();
		return m_pText->Layout(m_Font, m_FontSize, Text, nullptr);
	}

	base::vec2 CPainter::MeasureText(float Size, const std::string &Text)
	{
		const float SavedSize = m_FontSize;
		m_FontSize = Size;
		const float Width = TextBounds(0.0f, 0.0f, Text);
		const float Height = LineHeight();
		m_FontSize = SavedSize;
		return base::vec2(Width, Height);
	}

	float CPainter::MeasureString(int Face, float Size, const std::string &Text)
	{
		if(m_pText == nullptr)
			return 0.0f;
		return m_pText->Layout(Face, Size, Text, nullptr);
	}

	SDL_Texture *CPainter::LookupTexture(int Texture) const
	{
		if(m_pTextureLookup == nullptr || Texture < 0)
			return nullptr;
		return m_pTextureLookup(m_pTextureUser, Texture);
	}

	SDL_Texture *CPainter::GlyphAtlas()
	{
		if(m_pText == nullptr || m_pRenderer == nullptr)
			return nullptr;
		const int Width = m_pText->AtlasWidth();
		const int Height = m_pText->AtlasHeight();
		if(Width <= 0 || Height <= 0)
			return nullptr;

		if(m_pGlyphTexture == nullptr || m_GlyphTextureWidth != Width || m_GlyphTextureHeight != Height)
		{
			if(m_pGlyphTexture != nullptr)
				SDL_DestroyTexture(m_pGlyphTexture);
			// the atlas is coverage only, and the glyph colour comes from the
			// vertex colour, so it is uploaded as white with the coverage in
			// the alpha channel; a streaming texture is used because it is
			// rewritten whenever a new glyph is rasterized
			m_pGlyphTexture = SDL_CreateTexture(m_pRenderer, SDL_PIXELFORMAT_RGBA32,
				SDL_TEXTUREACCESS_STREAMING, Width, Height);
			if(m_pGlyphTexture == nullptr)
			{
				LOG_ERROR("painter", "failed to create the glyph atlas texture: %s", SDL_GetError());
				return nullptr;
			}
			SDL_SetTextureBlendMode(m_pGlyphTexture, SDL_BLENDMODE_BLEND);
			SDL_SetTextureScaleMode(m_pGlyphTexture, SDL_SCALEMODE_LINEAR);
			m_GlyphTextureWidth = Width;
			m_GlyphTextureHeight = Height;
			m_UploadedAtlasRevision = 0;
			// every texel outside a glyph is transparent; the texture is not
			// re-cleared on growth, so start from zero
			m_GlyphScratch.assign(static_cast<std::size_t>(Width) * Height, 0u);
		}

		if(m_UploadedAtlasRevision != m_pText->AtlasRevision())
		{
			const storage::byte_vector &Pixels = m_pText->AtlasPixels();
			if(Pixels.size() >= static_cast<std::size_t>(Width) * Height)
			{
				if(m_GlyphScratch.size() < static_cast<std::size_t>(Width) * Height)
					m_GlyphScratch.assign(static_cast<std::size_t>(Width) * Height, 0u);

				// white rgb with the coverage as alpha
				for(std::size_t i = 0; i < m_GlyphScratch.size(); i++)
				{
					const std::uint32_t Coverage = static_cast<unsigned char>(Pixels[i]);
					m_GlyphScratch[i] = (Coverage << 24) | 0x00FFFFFFu;
				}
				SDL_UpdateTexture(m_pGlyphTexture, nullptr, m_GlyphScratch.data(), Width * 4);
			}
			m_UploadedAtlasRevision = m_pText->AtlasRevision();
		}
		return m_pGlyphTexture;
	}

	void CPainter::Text(float X, float Y, const std::string &Text)
	{
		if(m_pText == nullptr || Text.empty())
			return;
		m_GlyphQuads.clear();
		const float Width = m_pText->Layout(m_Font, m_FontSize, Text, &m_GlyphQuads);
		if(m_GlyphQuads.empty())
			return;

		float OffsetX = 0.0f;
		if(m_Align == 1)
			OffsetX = -Width * 0.5f;
		else if(m_Align == 2)
			OffsetX = -Width;

		SDL_Texture *pAtlas = GlyphAtlas();
		if(pAtlas == nullptr)
			return;

		const float AtlasWidth = static_cast<float>(m_pText->AtlasWidth());
		const float AtlasHeight = static_cast<float>(m_pText->AtlasHeight());
		const base::vec2 Origin = Transform(X + OffsetX, Y);

		std::vector<CVertex> Vertices;
		Vertices.reserve(m_GlyphQuads.size() * 6);
		for(const CGlyphQuad &Quad : m_GlyphQuads)
		{
			const float X0 = Origin.x + Quad.m_X * m_State.m_ScaleX;
			const float Y0 = Origin.y + Quad.m_Y * m_State.m_ScaleY;
			const float X1 = X0 + Quad.m_Width * m_State.m_ScaleX;
			const float Y1 = Y0 + Quad.m_Height * m_State.m_ScaleY;
			const float U0 = Quad.m_U0 / AtlasWidth;
			const float V0 = Quad.m_V0 / AtlasHeight;
			const float U1 = Quad.m_U1 / AtlasWidth;
			const float V1 = Quad.m_V1 / AtlasHeight;

			const CVertex A{X0, Y0, U0, V0, m_FillColor.r, m_FillColor.g, m_FillColor.b, m_FillColor.a};
			const CVertex B{X1, Y0, U1, V0, m_FillColor.r, m_FillColor.g, m_FillColor.b, m_FillColor.a};
			const CVertex C{X1, Y1, U1, V1, m_FillColor.r, m_FillColor.g, m_FillColor.b, m_FillColor.a};
			const CVertex D{X0, Y1, U0, V1, m_FillColor.r, m_FillColor.g, m_FillColor.b, m_FillColor.a};
			Vertices.push_back(A);
			Vertices.push_back(B);
			Vertices.push_back(C);
			Vertices.push_back(A);
			Vertices.push_back(C);
			Vertices.push_back(D);
		}
		EmitTriangles(Vertices, pAtlas, true);
	}
} // namespace engine::painter
