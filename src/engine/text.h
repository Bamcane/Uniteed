#ifndef ENGINE_TEXT_H
#define ENGINE_TEXT_H

/*
	Text shaping and rasterization: HarfBuzz turns a UTF-8 string into glyphs,
	FreeType rasterizes them into one shared 8-bit coverage atlas, and the
	painter draws the glyph quads from that atlas.

	Doing the shaping ourselves (rather than letting the backend lay out the
	string) is what makes the CJK fallback work: HarfBuzz reports the glyphs a
	font is missing, and those runs are reshaped with the fallback face.

	Only the glyphs the interface actually uses are rasterized, and the atlas
	grows as needed, so an editor with a couple of hundred labels keeps a small
	texture.
*/

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb-ot.h>
#include <hb.h>

#include "base/math.h"
#include "base/storage.h"

namespace engine
{
	// a font face plus everything HarfBuzz needs about it
	struct CFontFace
	{
		FT_Face m_Face = nullptr;
		hb_font_t *m_pHbFont = nullptr;
		hb_face_t *m_pHbFace = nullptr;
		// the file the face was opened from, kept alive while the face is:
		// FreeType points into it instead of copying
		storage::byte_vector m_FontData;
		// the size the metrics below were read at
		float m_Size = 0.0f;
		float m_Ascent = 0.0f;
		float m_Descent = 0.0f;
		float m_LineHeight = 0.0f;
		std::string m_Path;
	};

	// one placed glyph, in the string's local space
	struct CGlyphQuad
	{
		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_Width = 0.0f;
		float m_Height = 0.0f;
		// texel coordinates inside the atlas
		float m_U0 = 0.0f;
		float m_V0 = 0.0f;
		float m_U1 = 0.0f;
		float m_V1 = 0.0f;
	};

	// one rasterized glyph inside the atlas
	struct CGlyph
	{
		int m_AtlasX = 0;
		int m_AtlasY = 0;
		int m_Width = 0;
		int m_Height = 0;
		float m_BearingX = 0.0f;
		float m_BearingY = 0.0f;
		// cached at the size the rasterizer was asked for
		float m_Size = 0.0f;
		bool m_bValid = false;
	};

	class CTextEngine
	{
	public:
		~CTextEngine();

		// pPrimary is the interface font, pFallback the CJK one; either may be
		// empty, in which case only the other is used
		bool Init(const std::string &Primary, const std::string &PrimaryBold,
			const std::string &Fallback);

		void Close();

		// the atlas is 8 bit coverage, one byte per pixel; it starts small and
		// doubles until the glyphs fit
		const storage::byte_vector &AtlasPixels() const { return m_AtlasPixels; }
		int AtlasWidth() const { return m_AtlasWidth; }
		int AtlasHeight() const { return m_AtlasHeight; }
		// bumped whenever the atlas grew, so the painter re-uploads it
		std::uint64_t AtlasRevision() const { return m_AtlasRevision; }

		// the face a caller picks, 0 = regular and 1 = bold; both fall back to
		// the fallback face for the glyphs they are missing
		int FaceCount() const { return 2; }
		const std::string &FacePath(int Face) const;

		// the metrics are read from FreeType at the requested size and cached
		// on the face, so these are not const
		float Ascent(int Face, float Size);
		float Descent(int Face, float Size);
		float LineHeight(int Face, float Size);

		// shapes and places the string; when pOut is null only the advance is
		// computed, which is what the layout needs for measuring
		float Layout(int Face, float Size, const std::string &Text, std::vector<CGlyphQuad> *pOut);

	private:
		struct CKey
		{
			int m_Face = 0;
			std::uint32_t m_Glyph = 0;
			// the raster size in quarter pixels, so sizes that round to the
			// same bitmap share an entry
			int m_Size = 0;
			bool operator<(const CKey &Other) const
			{
				if(m_Face != Other.m_Face)
					return m_Face < Other.m_Face;
				if(m_Glyph != Other.m_Glyph)
					return m_Glyph < Other.m_Glyph;
				return m_Size < Other.m_Size;
			}
		};

		// a face slot: the regular and bold face, each with its fallback
		struct CSlot
		{
			CFontFace m_Primary;
			CFontFace m_Fallback;
			// which face a glyph came from, filled while shaping
			bool m_bHasPrimary = false;
			bool m_bHasFallback = false;
		};

		bool LoadFace(const std::string &Path, CFontFace &Face);
		// picks the face of a slot that can draw a codepoint, primary first
		CFontFace *FaceForCodepoint(CSlot &Slot, std::uint32_t Codepoint);
		// rasterizes a glyph index of a face if it is not cached yet, growing
		// the atlas first
		const CGlyph *Rasterize(int Face, CFontFace *pFace, std::uint32_t Glyph, float Size);
		bool GrowAtlas();
		// the next free spot in the current shelf
		void AllocGlyph(int Width, int Height, int &OutX, int &OutY);

		FT_Library m_pLibrary = nullptr;
		CSlot m_aSlots[2];
		std::string m_aPaths[2];

		std::map<CKey, CGlyph> m_Glyphs;

		// the atlas is a shelf packer: glyphs are laid out left to right on a
		// row, and a new row starts when the current one is full
		storage::byte_vector m_AtlasPixels;
		int m_AtlasWidth = 0;
		int m_AtlasHeight = 0;
		int m_ShelfX = 0;
		int m_ShelfY = 0;
		int m_ShelfHeight = 0;
		std::uint64_t m_AtlasRevision = 1;

		// scratch buffers, reused between calls
		std::vector<unsigned int> m_GlyphIds;
		std::vector<int> m_Clusters;
		std::vector<float> m_Advances;
	};
} // namespace engine

#endif // ENGINE_TEXT_H
