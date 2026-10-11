#ifndef ENGINE_PAINTER_H
#define ENGINE_PAINTER_H

/*
	The drawing surface the editor uses, a small immediate mode vector painter
	on top of SDL3's renderer.

	It keeps the vocabulary the interface was written against (a path is built,
	filled or stroked with a colour or a paint, and the transform and clip
	stacks can be pushed and popped), so the editor describes what it wants
	rather than which triangles to emit.

	Two things are worth knowing about the implementation:

	- SDL3's renderer clips only its own rect calls, not geometry, so the
	  painter keeps its own scissor stack and clamps every primitive it emits.
	- Fills are triangulated by ear clipping; strokes are emitted as one quad
	  per segment. That is enough for the interface, which draws rectangles,
	  circles and short polylines.
*/

#include <cstdint>
#include <string>
#include <vector>

#include <sugar_types.h>

#include "base/math.h"
#include "base/storage.h"
#include "engine/text.h"

struct SDL_Renderer;
struct SDL_Texture;

namespace engine::painter
{
	// a colour in the 0..1 range
	struct CColor
	{
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 1.0f;
	};

	inline CColor RGB(int R, int G, int B)
	{
		return CColor{static_cast<float>(R) / 255.0f, static_cast<float>(G) / 255.0f,
			static_cast<float>(B) / 255.0f, 1.0f};
	}

	inline CColor RGBA(int R, int G, int B, int A)
	{
		return CColor{static_cast<float>(R) / 255.0f, static_cast<float>(G) / 255.0f,
			static_cast<float>(B) / 255.0f, static_cast<float>(A) / 255.0f};
	}

	inline CColor RGBAf(float R, float G, float B, float A) { return CColor{R, G, B, A}; }
	inline CColor RGBf(float R, float G, float B) { return CColor{R, G, B, 1.0f}; }

	inline CColor WithAlpha(const CColor &Color, float Alpha)
	{
		return CColor{Color.r, Color.g, Color.b, Color.a * Alpha};
	}

	/*
		How a texture is sampled outside its 0..1 range. A tile atlas needs
		clamping: the last column of a sheet sits exactly at u = 1, and a wrap
		triggered by a rounding error would sample the opposite edge of the
		sheet into the tile. A quad that asks for more than one tile needs
		wrapping instead.
	*/
	enum class address_mode
	{
		automatic, // wrap only when the coordinates leave 0..1
		clamp,     // never wrap
		wrap,      // repeat
	};

	// the paint of a fill: a flat colour, a linear gradient or a texture
	struct CPaint
	{
		enum class kind
		{
			solid,
			linear_gradient,
			image,
		};

		kind m_Kind = kind::solid;
		CColor m_Color;

		/*
			What an image fill is multiplied by. White - the default - changes
			nothing, and the theme's colour turns a white drawing into a themed
			one, which is how one rasterized icon serves the normal, the hover
			and the disabled state of a button.
		*/
		CColor m_Tint = RGB(255, 255, 255);

		// linear gradient, in the space the path was built in
		base::vec2 m_Start;
		base::vec2 m_End;
		CColor m_Inner;
		CColor m_Outer;

		// image: the texture key, the rectangle it is mapped onto and the
		// texel window that is sampled from it
		int m_Texture = -1;
		base::vec2 m_ImageTopLeft;
		base::vec2 m_ImageSize;
		// the sampled rectangle, in texels of the source texture, and the size
		// of that texture; together they give the normalized coordinates the
		// renderer wants
		float m_TexX = 0.0f;
		float m_TexY = 0.0f;
		float m_TexWidth = 0.0f;
		float m_TexHeight = 0.0f;
		int m_TexTextureWidth = 0;
		int m_TexTextureHeight = 0;
		float m_Alpha = 1.0f;
		// how this fill samples outside 0..1
		address_mode m_Address = address_mode::automatic;
	};

	enum class line_cap
	{
		butt,
		round,
		square,
	};

	enum class line_join
	{
		miter,
		round,
		bevel,
	};

	enum class winding
	{
		clockwise,  // the default: the area inside the path
		counter_clockwise,
	};

	class CPainter
	{
	public:
		void Init(SDL_Renderer *pRenderer, CTextEngine *pText);
		void Close();

		// a frame begins with the size the interface lays out in
		void BeginFrame(float Width, float Height);
		void EndFrame();

		// ------------------------------------------------------- transforms
		void Save();
		void Restore();
		void Reset();
		void Translate(float X, float Y);
		void Rotate(float Radians);
		void Scale(float X, float Y);
		void Scissor(float X, float Y, float Width, float Height);
		void ResetScissor();

		// ------------------------------------------------------------ paths
		void BeginPath();
		void MoveTo(float X, float Y);
		void LineTo(float X, float Y);
		void Rect(float X, float Y, float Width, float Height);
		void RoundedRect(float X, float Y, float Width, float Height, float Radius);
		void Circle(float X, float Y, float Radius);
		void ClosePath();
		void PathWinding(winding Winding);

		void FillColor(const CColor &Color);
		void FillPaint(const CPaint &Paint);
		void Fill();
		// the address mode of the next textured draw; a paint overrides it
		void SetTextureAddressMode(address_mode Mode) { m_AddressMode = Mode; }
		void StrokeColor(const CColor &Color);
		void StrokeWidth(float Width);
		void Stroke();

		// ------------------------------------------------------------ text
		void FontFaceId(int Face);
		void FontSize(float Size);
		void FillColorText(const CColor &Color) { FillColor(Color); }
		void TextAlign(int Align);
		void Text(float X, float Y, const std::string &Text);
		// the width of the shaped string, without drawing it
		float TextBounds(float X, float Y, const std::string &Text);
		// the width of a string at an arbitrary size, which is what a layout
		// measures its labels with before it has set the size itself
		float MeasureString(int Face, float Size, const std::string &Text);
		// the shaped quads of the last Text() call, for a caller that wants to
		// tint or outline individual glyphs
		float LineHeight() const;

		// A textured quad whose corners each carry their own colour and texture
		// coordinate, subdivided into a Cells by Cells grid so a bilinear
		// surface can be shown. The corners are in the order the map stores a
		// quad in: top left, top right, bottom left, bottom right. This is what
		// draws a map quad, which is a freeform patch rather than a rectangle.
		void TexturedQuad(int Texture, const base::vec2 *paCorners, const base::vec2 *paTexcoords,
			const CColor *paColors, int Cells, float Alpha = 1.0f);

		// a filled rectangle and a line, the two shapes the widgets need most
		void FillRect(float X, float Y, float Width, float Height, const CColor &Color);
		void StrokeRect(float X, float Y, float Width, float Height, const CColor &Color, float Width2);
		void DrawLine(float X0, float Y0, float X1, float Y1, const CColor &Color, float Width2);
		void DrawCircle(float X, float Y, float Radius, const CColor &Color);

		// --------------------------------------------------------- textures
		// the painter uploads the glyph atlas itself, so a text call after a
		// growth re-uploads it; the caller only has to invalidate its own
		void InvalidateGlyphAtlas() { m_UploadedAtlasRevision = 0; }

		// the painter needs a way to turn a texture id into an SDL texture;
		// this is set by the graphics layer, which owns them
		using texture_lookup_fn = SDL_Texture *(*)(void *pUser, int Texture);
		void SetTextureLookup(texture_lookup_fn pLookup, void *pUser)
		{
			m_pTextureLookup = pLookup;
			m_pTextureUser = pUser;
		}

		base::vec2 MeasureText(float Size, const std::string &Text);

		// A path point. The triangulator works in screen space (m_X, m_Y),
		// while the paint of an image fill needs the position in the space the
		// path was built in (m_LocalX, m_LocalY): a tile is drawn translated, so
		// its texel window would otherwise be measured from the wrong origin.
		struct CPoint
		{
			float m_X, m_Y;
			float m_LocalX = 0.0f;
			float m_LocalY = 0.0f;
		};

	private:
		struct CVertex
		{
			float m_X, m_Y;
			float m_U, m_V;
			float m_R, m_G, m_B, m_A;
		};

		struct CState
		{
			float m_TranslateX = 0.0f;
			float m_TranslateY = 0.0f;
			float m_ScaleX = 1.0f;
			float m_ScaleY = 1.0f;
			float m_Rotation = 0.0f;
			// the clip in screen space
			bool m_bScissor = false;
			base::rect m_Scissor;
		};

		base::vec2 Transform(float X, float Y) const;
		// clamps a rectangle to the active scissor
		bool ClipRect(base::rect &Rect) const;

		void EmitTriangles(const std::vector<CVertex> &Vertices, SDL_Texture *pTexture, bool bTextured);
		void EmitSolid(const std::vector<CPoint> &Points, const CColor &Color);
		void EmitStroke(const std::vector<CPoint> &Points, bool bClosed, const CColor &Color, float Width);
		// triangulates the path by ear clipping and fills it with one colour
		void FillPolygon(const std::vector<CPoint> &Points, const CColor &Color);
		void FillPolygonPaint(const std::vector<CPoint> &Points, const CPaint &Paint);

		SDL_Texture *LookupTexture(int Texture) const;
		// makes sure the glyph atlas is uploaded and returns the texture
		SDL_Texture *GlyphAtlas();

		SDL_Renderer *m_pRenderer = nullptr;
		CTextEngine *m_pText = nullptr;

		std::vector<CState> m_Stack;
		CState m_State;

		std::vector<CPoint> m_Path;
		std::vector<CVertex> m_Vertices;
		CPoint m_PathStart{0.0f, 0.0f};
		// ClosePath() records that the outline should be closed; a stroke needs
		// to know, a fill closes implicitly either way
		bool m_bPathClosed = false;

		CColor m_FillColor = RGB(255, 255, 255);
		CPaint m_FillPaint;
		bool m_bFillIsPaint = false;
		CColor m_StrokeColor = RGB(255, 255, 255);
		float m_StrokeWidth = 1.0f;
		winding m_Winding = winding::clockwise;

		int m_Font = 0;
		float m_FontSize = 13.0f;
		int m_Align = 0; // 0 left, 1 center, 2 right
		CColor m_TextColor;
		std::vector<CGlyphQuad> m_GlyphQuads;

		texture_lookup_fn m_pTextureLookup = nullptr;
		void *m_pTextureUser = nullptr;

		address_mode m_AddressMode = address_mode::automatic;
		SDL_Texture *m_pGlyphTexture = nullptr;
		int m_GlyphTextureWidth = 0;
		int m_GlyphTextureHeight = 0;
		// the atlas expanded to rgba, reused between uploads
		std::vector<std::uint32_t> m_GlyphScratch;
		std::uint64_t m_UploadedAtlasRevision = 0;
	};
} // namespace engine::painter

#endif // ENGINE_PAINTER_H
