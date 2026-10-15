#include "render.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <vector>

#include "base/string.h"
#include "game/assets.h"
#include "game/map/envelope.h"

namespace game
{
	namespace
	{
		constexpr float TileSize = 32.0f;

		int TintChannel(int Value, float Scale)
		{
			return base::clamp(static_cast<int>(std::lround(static_cast<float>(Value) * Scale)), 0, 255);
		}
	} // namespace

	// The colour and point helpers, shared with the editor so a render and the
	// canvas cannot disagree about what a layer colour or an animated quad is.

	CColor LayerColorAt(const CMap &Map, const CLayer &Layer, float Time)
	{
		CEnvelopeSample Environment;
		Environment[0] = Environment[1] = Environment[2] = Environment[3] = 1.0f;
		if(Layer.m_ColorEnv >= 0 && Layer.m_ColorEnv < static_cast<int>(Map.m_vEnvelopes.size()))
			EvaluateEnvelope(Map.m_vEnvelopes[static_cast<std::size_t>(Layer.m_ColorEnv)],
				Time + Layer.m_ColorEnvOffset / 1000.0f, Environment, 4);

		CColor Color;
		Color.r = TintChannel(Layer.m_Color.r, Environment[0]);
		Color.g = TintChannel(Layer.m_Color.g, Environment[1]);
		Color.b = TintChannel(Layer.m_Color.b, Environment[2]);
		Color.a = TintChannel(Layer.m_Color.a, Environment[3]);
		return Color;
	}

	void QuadColorsAt(const CMap &Map, const CQuad &Quad, float Time, CColor *paColors)
	{
		CEnvelopeSample Environment;
		Environment[0] = Environment[1] = Environment[2] = Environment[3] = 1.0f;
		if(Quad.m_ColorEnv >= 0 && Quad.m_ColorEnv < static_cast<int>(Map.m_vEnvelopes.size()))
			EvaluateEnvelope(Map.m_vEnvelopes[static_cast<std::size_t>(Quad.m_ColorEnv)],
				Time + Quad.m_ColorEnvOffset / 1000.0f, Environment, 4);

		for(int i = 0; i < 4; i++)
		{
			paColors[i].r = TintChannel(Quad.m_aColors[i].r, Environment[0]);
			paColors[i].g = TintChannel(Quad.m_aColors[i].g, Environment[1]);
			paColors[i].b = TintChannel(Quad.m_aColors[i].b, Environment[2]);
			paColors[i].a = TintChannel(Quad.m_aColors[i].a, Environment[3]);
		}
	}

	void QuadPointsAt(const CMap &Map, const CQuad &Quad, float Time, base::vec2 *paPoints)
	{
		CEnvelopeSample Position;
		if(Quad.m_PosEnv >= 0 && Quad.m_PosEnv < static_cast<int>(Map.m_vEnvelopes.size()))
			EvaluateEnvelope(Map.m_vEnvelopes[static_cast<std::size_t>(Quad.m_PosEnv)],
				Time + Quad.m_PosEnvOffset / 1000.0f, Position, 3);

		const base::vec2 Offset(Position[0], Position[1]);
		const float Rotation = Position[2] / 180.0f * base::Pi;
		// the fifth point is the pivot the rotation turns around
		const base::vec2 Pivot(base::FixedToFloat(Quad.m_aPoints[4].x), base::FixedToFloat(Quad.m_aPoints[4].y));

		for(int i = 0; i < 4; i++)
		{
			base::vec2 Point(base::FixedToFloat(Quad.m_aPoints[i].x), base::FixedToFloat(Quad.m_aPoints[i].y));
			if(Rotation != 0.0f)
			{
				const base::vec2 Relative = Point - Pivot;
				const float Cos = std::cos(Rotation);
				const float Sin = std::sin(Rotation);
				Point = base::vec2(Relative.x * Cos - Relative.y * Sin, Relative.x * Sin + Relative.y * Cos) + Pivot;
			}
			paPoints[i] = Point + Offset;
		}
	}

	namespace
	{
		// -------------------------------------------------------------- images
		struct CImagePixels
		{
			int m_Width = 0;
			int m_Height = 0;
			std::vector<unsigned char> m_Pixels; // RGBA
		};

		/*
			The images the render touches, decoded once each. An embedded image
			already carries raw RGBA, an external one is looked up through the
			active version's mapres paths and decoded with the same decoder the
			editor uses.
		*/
		class CImageCache
		{
		public:
			explicit CImageCache(const CMap &Map) : m_Map(Map) {}

			const CImagePixels *Get(int Index)
			{
				if(Index < 0 || Index >= static_cast<int>(m_Map.m_vImages.size()))
					return nullptr;
				const auto Found = m_Cache.find(Index);
				if(Found != m_Cache.end())
					return Found->second.m_bValid ? &Found->second.m_Pixels : nullptr;

				CEntry Entry;
				Entry.m_Pixels = Load(Index);
				Entry.m_bValid = !Entry.m_Pixels.m_Pixels.empty();
				if(Entry.m_bValid)
					m_Loaded++;
				else
					m_Missing++;
				const auto Inserted = m_Cache.emplace(Index, std::move(Entry));
				return Inserted.first->second.m_bValid ? &Inserted.first->second.m_Pixels : nullptr;
			}

			int Loaded() const { return m_Loaded; }
			int Missing() const { return m_Missing; }

		private:
			struct CEntry
			{
				CImagePixels m_Pixels;
				bool m_bValid = false;
			};

			CImagePixels Load(int Index)
			{
				CImagePixels Out;
				const CImage &Image = m_Map.m_vImages[static_cast<std::size_t>(Index)];
				if(Image.IsEmbedded())
				{
					if(Image.m_Width <= 0 || Image.m_Height <= 0)
						return Out;
					const std::size_t Count = static_cast<std::size_t>(Image.m_Width) * Image.m_Height * 4;
					if(Image.m_Data.size() < Count)
						return Out;
					Out.m_Width = Image.m_Width;
					Out.m_Height = Image.m_Height;
					Out.m_Pixels.assign(reinterpret_cast<const unsigned char *>(Image.m_Data.data()),
						reinterpret_cast<const unsigned char *>(Image.m_Data.data()) + Count);
					return Out;
				}
				const std::optional<storage::path> File = FindImageFile(Image.m_Name);
				if(!File.has_value())
					return Out;
				const std::optional<storage::byte_vector> Bytes = storage::read(*File);
				if(!Bytes.has_value())
					return Out;
				const std::optional<CDecodedImage> Decoded = DecodePng(*Bytes);
				if(!Decoded.has_value())
					return Out;
				Out.m_Width = Decoded->m_Width;
				Out.m_Height = Decoded->m_Height;
				Out.m_Pixels.assign(reinterpret_cast<const unsigned char *>(Decoded->m_Pixels.data()),
					reinterpret_cast<const unsigned char *>(Decoded->m_Pixels.data()) + Decoded->m_Pixels.size());
				return Out;
			}

			const CMap &m_Map;
			std::map<int, CEntry> m_Cache;
			int m_Loaded = 0;
			int m_Missing = 0;
		};

		// ---------------------------------------------------------- framebuffer
		struct CFramebuffer
		{
			int m_Width = 0;
			int m_Height = 0;
			std::vector<unsigned char> m_Pixels; // RGBA

			unsigned char *At(int X, int Y)
			{
				return &m_Pixels[(static_cast<std::size_t>(Y) * m_Width + X) * 4];
			}
		};

		// straight alpha "source over destination", so the background can be
		// transparent as easily as it can be an opaque colour
		void BlendOver(unsigned char *Dst, const unsigned char Src[4])
		{
			const float Sa = Src[3] / 255.0f;
			if(Sa <= 0.0f)
				return;
			const float Da = Dst[3] / 255.0f;
			const float OutA = Sa + Da * (1.0f - Sa);
			if(OutA <= 0.0f)
			{
				Dst[0] = Dst[1] = Dst[2] = Dst[3] = 0;
				return;
			}
			for(int c = 0; c < 3; c++)
				Dst[c] = static_cast<unsigned char>(base::clamp(
					static_cast<int>(std::lround((Src[c] * Sa + Dst[c] * Da * (1.0f - Sa)) / OutA)), 0, 255));
			Dst[3] = static_cast<unsigned char>(base::clamp(static_cast<int>(std::lround(OutA * 255.0f)), 0, 255));
		}

		// ------------------------------------------------------------ geometry
		// x' = A x + C y + E, y' = B x + D y + F; the tile transform the editor
		// pushes is expressed here in the same order it pushes it
		struct CAffine
		{
			float m_A = 1.0f, m_B = 0.0f, m_C = 0.0f, m_D = 1.0f, m_E = 0.0f, m_F = 0.0f;

			base::vec2 Apply(const base::vec2 &P) const
			{
				return base::vec2(m_A * P.x + m_C * P.y + m_E, m_B * P.x + m_D * P.y + m_F);
			}
		};

		CAffine Multiply(const CAffine &M, const CAffine &N)
		{
			return CAffine{
				M.m_A * N.m_A + M.m_C * N.m_B,
				M.m_B * N.m_A + M.m_D * N.m_B,
				M.m_A * N.m_C + M.m_C * N.m_D,
				M.m_B * N.m_C + M.m_D * N.m_D,
				M.m_A * N.m_E + M.m_C * N.m_F + M.m_E,
				M.m_B * N.m_E + M.m_D * N.m_F + M.m_F};
		}

		bool Invert(const CAffine &M, CAffine &Out)
		{
			const float Det = M.m_A * M.m_D - M.m_B * M.m_C;
			if(std::abs(Det) < 1e-9f)
				return false;
			const float Inv = 1.0f / Det;
			Out.m_A = M.m_D * Inv;
			Out.m_B = -M.m_B * Inv;
			Out.m_C = -M.m_C * Inv;
			Out.m_D = M.m_A * Inv;
			Out.m_E = (M.m_C * M.m_F - M.m_D * M.m_E) * Inv;
			Out.m_F = (M.m_B * M.m_E - M.m_A * M.m_F) * Inv;
			return true;
		}

		CAffine TileTransform(float X, float Y, float Size, int Flags)
		{
			CAffine M;
			M = Multiply(M, CAffine{1.0f, 0.0f, 0.0f, 1.0f, X, Y});
			if(Flags & TILEFLAG_ROTATE)
			{
				const float C = std::cos(base::Pi * 0.5f), S = std::sin(base::Pi * 0.5f);
				M = Multiply(M, Multiply(CAffine{1.0f, 0.0f, 0.0f, 1.0f, Size, 0.0f}, CAffine{C, S, -S, C, 0.0f, 0.0f}));
			}
			if(Flags & TILEFLAG_HFLIP)
				M = Multiply(M, Multiply(CAffine{1.0f, 0.0f, 0.0f, 1.0f, Size, 0.0f}, CAffine{-1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}));
			if(Flags & TILEFLAG_VFLIP)
				M = Multiply(M, Multiply(CAffine{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, Size}, CAffine{1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f}));
			// the local space is 0..1, so the tile's own size is the last scale
			// the editor leaves implicit in its 0..Size rectangle
			M = Multiply(M, CAffine{Size, 0.0f, 0.0f, Size, 0.0f, 0.0f});
			return M;
		}

		base::rect ComputeMapBounds(const CMap &Map)
		{
			float MaxX = 0.0f, MaxY = 0.0f;
			for(const CGroup &Group : Map.m_vGroups)
				for(const CLayer &Layer : Group.m_vLayers)
					if(Layer.IsTiles())
					{
						MaxX = std::max(MaxX, Layer.m_Width * TileSize);
						MaxY = std::max(MaxY, Layer.m_Height * TileSize);
					}
			return base::rect(base::vec2(0.0f, 0.0f), base::vec2(MaxX, MaxY));
		}

		// ----------------------------------------------------------- sampling
		void SampleBilinear(const CImagePixels &Image, float X, float Y, bool bWrap, unsigned char Out[4])
		{
			const float W = static_cast<float>(Image.m_Width);
			const float H = static_cast<float>(Image.m_Height);
			if(bWrap)
			{
				X = X - std::floor(X / W) * W;
				Y = Y - std::floor(Y / H) * H;
			}
			X = base::clamp(X, 0.0f, W - 1.0f);
			Y = base::clamp(Y, 0.0f, H - 1.0f);
			const int X0 = static_cast<int>(std::floor(X));
			const int Y0 = static_cast<int>(std::floor(Y));
			const int X1 = std::min(X0 + 1, Image.m_Width - 1);
			const int Y1 = std::min(Y0 + 1, Image.m_Height - 1);
			const float Fx = X - X0;
			const float Fy = Y - Y0;
			for(int c = 0; c < 4; c++)
			{
				const float P00 = Image.m_Pixels[(static_cast<std::size_t>(Y0) * Image.m_Width + X0) * 4 + c];
				const float P10 = Image.m_Pixels[(static_cast<std::size_t>(Y0) * Image.m_Width + X1) * 4 + c];
				const float P01 = Image.m_Pixels[(static_cast<std::size_t>(Y1) * Image.m_Width + X0) * 4 + c];
				const float P11 = Image.m_Pixels[(static_cast<std::size_t>(Y1) * Image.m_Width + X1) * 4 + c];
				const float Top = P00 + (P10 - P00) * Fx;
				const float Bottom = P01 + (P11 - P01) * Fx;
				Out[c] = static_cast<unsigned char>(base::clamp(
					static_cast<int>(std::lround(Top + (Bottom - Top) * Fy)), 0, 255));
			}
		}

		// -------------------------------------------------------------- tiles
		void DrawTile(CFramebuffer &Fb, const base::rect &Clip, const CImagePixels &Image,
			int Index, int Flags, const base::vec2 &Screen, float Size, float Alpha, const CColor &Tint)
		{
			if(Index <= 0 || Size <= 0.0f || Alpha <= 0.0f)
				return;
			const CAffine M = TileTransform(Screen.x, Screen.y, Size, Flags);
			CAffine Inv;
			if(!Invert(M, Inv))
				return;

			// the cell of the 16 by 16 sheet, sampled with a clamp so a rounding
			// error cannot pull in the neighbouring tile
			const int Col = Index % 16;
			const int Row = Index / 16;
			const float CellW = static_cast<float>(Image.m_Width) / 16.0f;
			const float CellH = static_cast<float>(Image.m_Height) / 16.0f;
			const int CellX0 = static_cast<int>(std::floor(Col * CellW));
			const int CellY0 = static_cast<int>(std::floor(Row * CellH));
			const int CellX1 = std::max(CellX0, static_cast<int>(std::floor((Col + 1) * CellW)) - 1);
			const int CellY1 = std::max(CellY0, static_cast<int>(std::floor((Row + 1) * CellH)) - 1);

			const base::vec2 aCorner[4] = {M.Apply(base::vec2(0.0f, 0.0f)), M.Apply(base::vec2(1.0f, 0.0f)),
				M.Apply(base::vec2(0.0f, 1.0f)), M.Apply(base::vec2(1.0f, 1.0f))};
			float MinX = aCorner[0].x, MaxX = aCorner[0].x, MinY = aCorner[0].y, MaxY = aCorner[0].y;
			for(int i = 1; i < 4; i++)
			{
				MinX = std::min(MinX, aCorner[i].x);
				MaxX = std::max(MaxX, aCorner[i].x);
				MinY = std::min(MinY, aCorner[i].y);
				MaxY = std::max(MaxY, aCorner[i].y);
			}
			const int X0 = std::max(static_cast<int>(std::floor(MinX)), static_cast<int>(Clip.m_TopLeft.x));
			const int Y0 = std::max(static_cast<int>(std::floor(MinY)), static_cast<int>(Clip.m_TopLeft.y));
			const int X1 = std::min(static_cast<int>(std::ceil(MaxX)), static_cast<int>(Clip.m_BottomRight.x));
			const int Y1 = std::min(static_cast<int>(std::ceil(MaxY)), static_cast<int>(Clip.m_BottomRight.y));

			const int TintR = Tint.r, TintG = Tint.g, TintB = Tint.b;
			for(int y = Y0; y < Y1; y++)
			{
				for(int x = X0; x < X1; x++)
				{
					const base::vec2 Local = Inv.Apply(base::vec2(x + 0.5f, y + 0.5f));
					if(Local.x < 0.0f || Local.x > 1.0f || Local.y < 0.0f || Local.y > 1.0f)
						continue;
					int SX = static_cast<int>(std::floor(Col * CellW + Local.x * CellW));
					int SY = static_cast<int>(std::floor(Row * CellH + Local.y * CellH));
					SX = base::clamp(SX, CellX0, CellX1);
					SY = base::clamp(SY, CellY0, CellY1);
					const unsigned char *Tex = &Image.m_Pixels[(static_cast<std::size_t>(SY) * Image.m_Width + SX) * 4];
					unsigned char Src[4];
					Src[0] = static_cast<unsigned char>(Tex[0] * TintR / 255);
					Src[1] = static_cast<unsigned char>(Tex[1] * TintG / 255);
					Src[2] = static_cast<unsigned char>(Tex[2] * TintB / 255);
					Src[3] = static_cast<unsigned char>(std::lround(Tex[3] * Alpha));
					BlendOver(Fb.At(x, y), Src);
				}
			}
		}

		// -------------------------------------------------------------- quads
		/*
			A quad is a bilinear patch: corners 0 and 1 are the top edge and 2 and
			3 the bottom one, and a point is placed by bilinear interpolation of
			those four, exactly as the painter's TexturedQuad does. Inverting that
			per destination pixel means the patch can be sampled without splitting
			it into triangles, so a semi transparent quad has no seam along a
			diagonal.
		*/
		struct CPatchCorner
		{
			base::vec2 m_Pos;
			base::vec2 m_Tex;
			float m_Color[4];
		};

		bool InvertBilinear(const CPatchCorner aCorner[4], const base::vec2 &P, float &A, float &B)
		{
			const base::vec2 CA = aCorner[1].m_Pos - aCorner[0].m_Pos;
			const base::vec2 CB = aCorner[2].m_Pos - aCorner[0].m_Pos;
			const base::vec2 CC = aCorner[3].m_Pos - aCorner[1].m_Pos - aCorner[2].m_Pos + aCorner[0].m_Pos;
			const base::vec2 R = P - aCorner[0].m_Pos;
			const float Det = CA.x * CB.y - CA.y * CB.x;
			if(std::abs(Det) < 1e-9f)
				return false;
			A = (R.x * CB.y - R.y * CB.x) / Det;
			B = (CA.x * R.y - CA.y * R.x) / Det;
			for(int It = 0; It < 4; It++)
			{
				const base::vec2 F = aCorner[0].m_Pos + CA * A + CB * B + CC * (A * B) - P;
				const base::vec2 JA = CA + CC * B;
				const base::vec2 JB = CB + CC * A;
				const float JDet = JA.x * JB.y - JA.y * JB.x;
				if(std::abs(JDet) < 1e-9f)
					break;
				const float DA = (F.x * JB.y - F.y * JB.x) / JDet;
				const float DB = (JA.x * F.y - JA.y * F.x) / JDet;
				A -= DA;
				B -= DB;
				if(std::abs(DA) + std::abs(DB) < 1e-5f)
					break;
			}
			return true;
		}

		float Bilinear1(float P00, float P10, float P01, float P11, float A, float B)
		{
			return P00 + (P10 - P00) * A + (P01 - P00) * B + (P11 - P10 - P01 + P00) * (A * B);
		}

		base::vec2 Bilinear2(const base::vec2 &P00, const base::vec2 &P10, const base::vec2 &P01, const base::vec2 &P11, float A, float B)
		{
			return P00 + (P10 - P00) * A + (P01 - P00) * B + (P11 - P10 - P01 + P00) * (A * B);
		}

		void DrawPatch(CFramebuffer &Fb, const base::rect &Clip, const CPatchCorner aCorner[4],
			const CImagePixels *pImage, bool bWrap)
		{
			float MinX = aCorner[0].m_Pos.x, MaxX = aCorner[0].m_Pos.x, MinY = aCorner[0].m_Pos.y, MaxY = aCorner[0].m_Pos.y;
			for(int i = 1; i < 4; i++)
			{
				MinX = std::min(MinX, aCorner[i].m_Pos.x);
				MaxX = std::max(MaxX, aCorner[i].m_Pos.x);
				MinY = std::min(MinY, aCorner[i].m_Pos.y);
				MaxY = std::max(MaxY, aCorner[i].m_Pos.y);
			}
			const int X0 = std::max(static_cast<int>(std::floor(MinX)), static_cast<int>(Clip.m_TopLeft.x));
			const int Y0 = std::max(static_cast<int>(std::floor(MinY)), static_cast<int>(Clip.m_TopLeft.y));
			const int X1 = std::min(static_cast<int>(std::ceil(MaxX)), static_cast<int>(Clip.m_BottomRight.x));
			const int Y1 = std::min(static_cast<int>(std::ceil(MaxY)), static_cast<int>(Clip.m_BottomRight.y));

			for(int y = Y0; y < Y1; y++)
			{
				for(int x = X0; x < X1; x++)
				{
					float A = 0.0f, B = 0.0f;
					if(!InvertBilinear(aCorner, base::vec2(x + 0.5f, y + 0.5f), A, B))
						continue;
					if(A < -1e-3f || A > 1.0f + 1e-3f || B < -1e-3f || B > 1.0f + 1e-3f)
						continue;

					float aColor[4];
					for(int c = 0; c < 4; c++)
						aColor[c] = Bilinear1(aCorner[0].m_Color[c], aCorner[1].m_Color[c],
							aCorner[2].m_Color[c], aCorner[3].m_Color[c], A, B);

					unsigned char Src[4];
					if(pImage != nullptr)
					{
						const base::vec2 Tex = Bilinear2(aCorner[0].m_Tex, aCorner[1].m_Tex,
							aCorner[2].m_Tex, aCorner[3].m_Tex, A, B);
						unsigned char Texel[4];
						SampleBilinear(*pImage, Tex.x * pImage->m_Width, Tex.y * pImage->m_Height, bWrap, Texel);
						for(int c = 0; c < 3; c++)
							Src[c] = static_cast<unsigned char>(base::clamp(
								static_cast<int>(std::lround(Texel[c] * aColor[c] / 255.0f)), 0, 255));
						Src[3] = static_cast<unsigned char>(base::clamp(
							static_cast<int>(std::lround(Texel[3] * aColor[3] / 255.0f)), 0, 255));
					}
					else
					{
						for(int c = 0; c < 4; c++)
							Src[c] = static_cast<unsigned char>(base::clamp(static_cast<int>(std::lround(aColor[c])), 0, 255));
					}
					BlendOver(Fb.At(x, y), Src);
				}
			}
		}

		CPatchCorner QuadCorner(const base::vec2 *paPos, const base::vec2 *paTex, const float aColor[4][4],
			bool bTextured, float U, float V)
		{
			CPatchCorner Out;
			const base::vec2 TopPoint = paPos[0] + (paPos[1] - paPos[0]) * U;
			const base::vec2 BottomPoint = paPos[2] + (paPos[3] - paPos[2]) * U;
			Out.m_Pos = TopPoint + (BottomPoint - TopPoint) * V;
			if(bTextured)
			{
				const base::vec2 TopTex = paTex[0] + (paTex[1] - paTex[0]) * U;
				const base::vec2 BottomTex = paTex[2] + (paTex[3] - paTex[2]) * U;
				Out.m_Tex = TopTex + (BottomTex - TopTex) * V;
			}
			for(int c = 0; c < 4; c++)
			{
				const float Top = base::lerp(aColor[0][c], aColor[1][c], U);
				const float Bottom = base::lerp(aColor[2][c], aColor[3][c], U);
				Out.m_Color[c] = base::lerp(Top, Bottom, V);
			}
			return Out;
		}

		// ------------------------------------------------------------- layers
		void DrawTileLayer(CFramebuffer &Fb, const base::rect &Clip, const CMap &Map, const CLayer &Layer,
			const base::view_transform &View, const CMapRenderOptions &Options, CImageCache &Cache, CMapRenderStats &Stats)
		{
			const CColor Color = LayerColorAt(Map, Layer, Options.m_Time);
			const float Alpha = static_cast<float>(Color.a) / 255.0f;
			if(Alpha <= 0.0f)
				return;
			const CImagePixels *pImage = Cache.Get(Layer.m_Image);
			if(pImage == nullptr)
				return; // no texture, nothing to show for the tiles

			const base::rect World = View.VisibleWorld();
			const int x0 = std::max(0, static_cast<int>(std::floor(World.m_TopLeft.x / TileSize)) - 1);
			const int y0 = std::max(0, static_cast<int>(std::floor(World.m_TopLeft.y / TileSize)) - 1);
			const int x1 = std::min(Layer.m_Width, static_cast<int>(std::ceil(World.m_BottomRight.x / TileSize)) + 1);
			const int y1 = std::min(Layer.m_Height, static_cast<int>(std::ceil(World.m_BottomRight.y / TileSize)) + 1);
			const float Size = TileSize * View.m_Zoom;

			for(int y = y0; y < y1; y++)
				for(int x = x0; x < x1; x++)
				{
					const CTile *pTile = Layer.Tile(x, y);
					if(pTile == nullptr || pTile->m_Index == 0)
						continue;
					const base::vec2 Screen = View.WorldToScreen(base::vec2(x * TileSize, y * TileSize));
					DrawTile(Fb, Clip, *pImage, pTile->m_Index, pTile->m_Flags, Screen, Size, Alpha, Color);
					Stats.m_Tiles++;
				}
		}

		void DrawQuadLayer(CFramebuffer &Fb, const base::rect &Clip, const CMap &Map, const CLayer &Layer,
			const base::view_transform &View, const CMapRenderOptions &Options, CImageCache &Cache, CMapRenderStats &Stats)
		{
			const CImagePixels *pImage = Layer.m_Image >= 0 ? Cache.Get(Layer.m_Image) : nullptr;
			const bool bTextured = pImage != nullptr;

			for(const CQuad &Quad : Layer.m_vQuads)
			{
				base::vec2 aWorld[4];
				QuadPointsAt(Map, Quad, Options.m_Time, aWorld);
				base::vec2 aPos[4];
				for(int i = 0; i < 4; i++)
					aPos[i] = View.WorldToScreen(aWorld[i]);

				CColor aQuadColors[4];
				QuadColorsAt(Map, Quad, Options.m_Time, aQuadColors);
				if(aQuadColors[0].a <= 0 && aQuadColors[1].a <= 0 && aQuadColors[2].a <= 0 && aQuadColors[3].a <= 0)
					continue;

				float aColor[4][4];
				for(int i = 0; i < 4; i++)
				{
					aColor[i][0] = static_cast<float>(aQuadColors[i].r);
					aColor[i][1] = static_cast<float>(aQuadColors[i].g);
					aColor[i][2] = static_cast<float>(aQuadColors[i].b);
					aColor[i][3] = static_cast<float>(aQuadColors[i].a);
				}

				base::vec2 aTex[4];
				bool bWrap = false;
				if(bTextured)
					for(int i = 0; i < 4; i++)
					{
						aTex[i] = base::vec2(base::FixedToFloat(Quad.m_aTexcoords[i].x), base::FixedToFloat(Quad.m_aTexcoords[i].y));
						if(aTex[i].x < 0.0f || aTex[i].x > 1.0f || aTex[i].y < 0.0f || aTex[i].y > 1.0f)
							bWrap = true;
					}

				const CPatchCorner aCorner[4] = {
					QuadCorner(aPos, aTex, aColor, bTextured, 0.0f, 0.0f),
					QuadCorner(aPos, aTex, aColor, bTextured, 1.0f, 0.0f),
					QuadCorner(aPos, aTex, aColor, bTextured, 0.0f, 1.0f),
					QuadCorner(aPos, aTex, aColor, bTextured, 1.0f, 1.0f)};
				DrawPatch(Fb, Clip, aCorner, bTextured ? pImage : nullptr, bWrap);
				Stats.m_Quads++;
			}
		}
	} // namespace

	// ------------------------------------------------------------------ public
	bool RenderMapToRgba(const CMap &Map, const CMapRenderOptions &Options,
		storage::byte_vector &OutPixels, CMapRenderStats &Stats, std::string &Error)
	{
		if(Options.m_Width <= 0 || Options.m_Height <= 0)
		{
			Error = "the render size is empty";
			return false;
		}
		if(Map.m_vGroups.empty())
		{
			Error = "the map has no groups";
			return false;
		}

		// the same mapres search order the window would use for this map
		SetupResourcePaths(MapVersionId(Map));

		CFramebuffer Fb;
		Fb.m_Width = Options.m_Width;
		Fb.m_Height = Options.m_Height;
		Fb.m_Pixels.assign(static_cast<std::size_t>(Fb.m_Width) * Fb.m_Height * 4, 0);
		if(!Options.m_bTransparent)
			for(std::size_t i = 0; i < Fb.m_Pixels.size(); i += 4)
			{
				Fb.m_Pixels[i + 0] = static_cast<unsigned char>(base::clamp(Options.m_Background.r, 0, 255));
				Fb.m_Pixels[i + 1] = static_cast<unsigned char>(base::clamp(Options.m_Background.g, 0, 255));
				Fb.m_Pixels[i + 2] = static_cast<unsigned char>(base::clamp(Options.m_Background.b, 0, 255));
				Fb.m_Pixels[i + 3] = static_cast<unsigned char>(base::clamp(Options.m_Background.a, 0, 255));
			}

		base::view_transform View;
		View.m_Origin = base::vec2(0.0f, 0.0f);
		View.m_Viewport = base::vec2(static_cast<float>(Options.m_Width), static_cast<float>(Options.m_Height));
		View.m_Zoom = Options.m_Zoom > 0.0f ? Options.m_Zoom : 1.0f;
		if(Options.m_bCenterSet)
			View.m_Center = Options.m_Center;
		else
		{
			const base::rect Bounds = ComputeMapBounds(Map);
			View.m_Center = Bounds.Width() > 0.0f && Bounds.Height() > 0.0f ? Bounds.Center() : base::vec2(0.0f, 0.0f);
		}

		const base::rect Full(base::vec2(0.0f, 0.0f), base::vec2(static_cast<float>(Options.m_Width), static_cast<float>(Options.m_Height)));
		CImageCache Cache(Map);

		for(const CGroup &Group : Map.m_vGroups)
		{
			Stats.m_Groups++;
			const base::view_transform GroupViewTransform = GroupView(View, Group);

			base::rect Clip = Full;
			if(Group.m_UseClipping && Group.m_ClipW > 0 && Group.m_ClipH > 0)
			{
				// the clip is in map space and projected with the plain view, the
				// way the reference editor clips
				const base::vec2 TopLeft = View.WorldToScreen(base::vec2(static_cast<float>(Group.m_ClipX), static_cast<float>(Group.m_ClipY)));
				const base::vec2 BottomRight = View.WorldToScreen(base::vec2(
					static_cast<float>(Group.m_ClipX + Group.m_ClipW), static_cast<float>(Group.m_ClipY + Group.m_ClipH)));
				Clip = base::rect(TopLeft, BottomRight).Intersect(Full);
			}
			if(Clip.Width() <= 0.0f || Clip.Height() <= 0.0f)
				continue;

			for(const CLayer &Layer : Group.m_vLayers)
			{
				// game and physics layers are editor affordances, not the map's
				// picture, so a render leaves them out; sounds have no picture
				if(!Layer.IsTiles() && !Layer.IsQuads())
					continue;
				if(Layer.IsGameLayer() || Layer.IsPhysicsLayer())
					continue;
				Stats.m_Layers++;
				if(Layer.IsTiles())
					DrawTileLayer(Fb, Clip, Map, Layer, GroupViewTransform, Options, Cache, Stats);
				else
					DrawQuadLayer(Fb, Clip, Map, Layer, GroupViewTransform, Options, Cache, Stats);
			}
		}

		Stats.m_Images = Cache.Loaded();
		Stats.m_MissingImages = Cache.Missing();

		OutPixels.assign(reinterpret_cast<const std::byte *>(Fb.m_Pixels.data()),
			reinterpret_cast<const std::byte *>(Fb.m_Pixels.data()) + Fb.m_Pixels.size());
		return true;
	}
} // namespace game
