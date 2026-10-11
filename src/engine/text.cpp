#include "text.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "base/logger.h"

namespace engine
{
	namespace
	{
		// the atlas starts at 512 and doubles; 4096 is the largest texture an
		// old GL3 implementation is guaranteed to accept
		constexpr int InitialAtlasSize = 512;
		constexpr int MaxAtlasSize = 4096;
		constexpr int AtlasPadding = 1;

		// the raster size is quantised so that 12.3 and 12.4 share a bitmap
		int QuantiseSize(float Size) { return static_cast<int>(std::lround(Size * 4.0f)); }
		float UnquantiseSize(int Size) { return static_cast<float>(Size) / 4.0f; }

		// decodes one UTF-8 sequence, advancing the index; malformed input
		// yields U+FFFD so a broken string still renders
		std::uint32_t NextCodepoint(const std::string &Text, std::size_t &Index)
		{
			const unsigned char First = static_cast<unsigned char>(Text[Index]);
			std::size_t Extra = 0;
			std::uint32_t Code = 0;
			if(First < 0x80)
			{
				Index++;
				return First;
			}
			else if((First & 0xE0) == 0xC0)
			{
				Extra = 1;
				Code = First & 0x1Fu;
			}
			else if((First & 0xF0) == 0xE0)
			{
				Extra = 2;
				Code = First & 0x0Fu;
			}
			else if((First & 0xF8) == 0xF0)
			{
				Extra = 3;
				Code = First & 0x07u;
			}
			else
			{
				Index++;
				return 0xFFFD;
			}

			if(Index + Extra >= Text.size())
			{
				Index = Text.size();
				return 0xFFFD;
			}
			for(std::size_t i = 1; i <= Extra; i++)
			{
				const unsigned char Byte = static_cast<unsigned char>(Text[Index + i]);
				if((Byte & 0xC0) != 0x80)
				{
					Index++;
					return 0xFFFD;
				}
				Code = (Code << 6) | (Byte & 0x3Fu);
			}
			Index += Extra + 1;
			return Code;
		}
	} // namespace

	CTextEngine::~CTextEngine() { Close(); }

	bool CTextEngine::LoadFace(const std::string &Path, CFontFace &Face)
	{
		if(Path.empty())
			return false;

		// Read into memory rather than opened by path: FT_New_Face takes a narrow
		// string, which Windows reads in the active code page, so a font under a
		// non ASCII directory would not open. The bytes are the same everywhere.
		const std::optional<storage::byte_vector> Bytes = storage::read(Path);
		if(!Bytes.has_value() || Bytes->empty())
		{
			LOG_WARN("text", "failed to read the font '{}'", Path.c_str());
			return false;
		}
		Face.m_FontData = *Bytes;

		if(FT_New_Memory_Face(m_pLibrary, reinterpret_cast<const FT_Byte *>(Face.m_FontData.data()),
			   static_cast<FT_Long>(Face.m_FontData.size()), 0, &Face.m_Face) != 0)
		{
			LOG_WARN("text", "failed to open the font '{}'", Path.c_str());
			Face.m_FontData.clear();
			Face.m_Face = nullptr;
			return false;
		}
		Face.m_pHbFace = hb_ft_face_create_referenced(Face.m_Face);
		if(Face.m_pHbFace == nullptr)
		{
			// the face goes first: FreeType reads the data while tearing down
			FT_Done_Face(Face.m_Face);
			Face.m_Face = nullptr;
			Face.m_FontData.clear();
			return false;
		}
		Face.m_pHbFont = hb_font_create(Face.m_pHbFace);
		if(Face.m_pHbFont == nullptr)
		{
			hb_face_destroy(Face.m_pHbFace);
			FT_Done_Face(Face.m_Face);
			Face.m_pHbFace = nullptr;
			Face.m_Face = nullptr;
			Face.m_FontData.clear();
			return false;
		}
		hb_ot_font_set_funcs(Face.m_pHbFont);
		Face.m_Path = Path;
		Face.m_Size = 0.0f;
		return true;
	}

	bool CTextEngine::Init(const std::string &Primary, const std::string &PrimaryBold, const std::string &Fallback)
	{
		if(FT_Init_FreeType(&m_pLibrary) != 0)
		{
			LOG_ERROR("text", "failed to initialise FreeType");
			return false;
		}

		// slot 0 is the interface font, slot 1 the bold one; each falls back to
		// the CJK face, so the two slots can share it
		m_aSlots[0].m_bHasPrimary = LoadFace(Primary, m_aSlots[0].m_Primary);
		m_aSlots[0].m_bHasFallback = LoadFace(Fallback, m_aSlots[0].m_Fallback);
		m_aSlots[1].m_bHasPrimary = LoadFace(PrimaryBold.empty() ? Primary : PrimaryBold, m_aSlots[1].m_Primary);
		m_aSlots[1].m_bHasFallback = LoadFace(Fallback, m_aSlots[1].m_Fallback);

		if(!m_aSlots[0].m_bHasPrimary && m_aSlots[0].m_bHasFallback)
		{
			// only the CJK face is around, use it for everything
			m_aSlots[0].m_Primary = m_aSlots[0].m_Fallback;
			m_aSlots[0].m_bHasPrimary = true;
		}
		if(!m_aSlots[0].m_bHasPrimary)
		{
			LOG_ERROR("text", "no usable font found");
			return false;
		}
		if(!m_aSlots[1].m_bHasPrimary)
		{
			m_aSlots[1].m_Primary = m_aSlots[0].m_Primary;
			m_aSlots[1].m_bHasPrimary = true;
		}

		m_aPaths[0] = m_aSlots[0].m_Primary.m_Path;
		m_aPaths[1] = m_aSlots[1].m_Primary.m_Path;

		m_AtlasWidth = InitialAtlasSize;
		m_AtlasHeight = InitialAtlasSize;
		m_AtlasPixels.assign(static_cast<std::size_t>(m_AtlasWidth) * m_AtlasHeight, std::byte{0});
		return true;
	}

	void CTextEngine::Close()
	{
		m_Glyphs.clear();
		m_AtlasPixels.clear();
		for(CSlot &Slot : m_aSlots)
		{
			CFontFace *aFaces[2] = {&Slot.m_Primary, &Slot.m_Fallback};
			for(CFontFace *pFace : aFaces)
			{
				if(pFace->m_pHbFont != nullptr)
					hb_font_destroy(pFace->m_pHbFont);
				if(pFace->m_pHbFace != nullptr)
					hb_face_destroy(pFace->m_pHbFace);
				if(pFace->m_Face != nullptr)
					FT_Done_Face(pFace->m_Face);
				pFace->m_pHbFont = nullptr;
				pFace->m_pHbFace = nullptr;
				pFace->m_Face = nullptr;
			}
		}
		if(m_pLibrary != nullptr)
		{
			FT_Done_FreeType(m_pLibrary);
			m_pLibrary = nullptr;
		}
	}

	const std::string &CTextEngine::FacePath(int Face) const
	{
		return m_aPaths[std::clamp(Face, 0, 1)];
	}

	// Picks the face of a slot that can render a codepoint, preferring the
	// primary one. The argument is a Unicode codepoint here: shaping decides the
	// face before anything is rasterized, and from then on the identity of a
	// glyph is a (face, glyph index) pair, not a codepoint.
	CFontFace *CTextEngine::FaceForCodepoint(CSlot &Slot, std::uint32_t Codepoint)
	{
		if(Slot.m_bHasPrimary && Slot.m_Primary.m_Face != nullptr)
		{
			if(FT_Get_Char_Index(Slot.m_Primary.m_Face, Codepoint) != 0)
				return &Slot.m_Primary;
		}
		if(Slot.m_bHasFallback && Slot.m_Fallback.m_Face != nullptr)
		{
			if(FT_Get_Char_Index(Slot.m_Fallback.m_Face, Codepoint) != 0)
				return &Slot.m_Fallback;
		}
		return Slot.m_bHasPrimary ? &Slot.m_Primary : (Slot.m_bHasFallback ? &Slot.m_Fallback : nullptr);
	}

	// reads the metrics for the size, caching them on the face
	static void UpdateMetrics(CFontFace &Face, float Size)
	{
		if(Face.m_Face == nullptr || Face.m_Size == Size)
			return;
		FT_Set_Pixel_Sizes(Face.m_Face, 0, static_cast<FT_UInt>(std::lround(Size)));
		Face.m_Size = Size;
		Face.m_Ascent = static_cast<float>(Face.m_Face->size->metrics.ascender) / 64.0f;
		Face.m_Descent = static_cast<float>(Face.m_Face->size->metrics.descender) / 64.0f;
		Face.m_LineHeight = static_cast<float>(Face.m_Face->size->metrics.height) / 64.0f;
	}

	float CTextEngine::Ascent(int Face, float Size)
	{
		CFontFace &Face0 = m_aSlots[std::clamp(Face, 0, 1)].m_Primary;
		if(Face0.m_Face == nullptr)
			return Size * 0.8f;
		UpdateMetrics(Face0, Size);
		return Face0.m_Ascent;
	}

	float CTextEngine::Descent(int Face, float Size)
	{
		CFontFace &Face0 = m_aSlots[std::clamp(Face, 0, 1)].m_Primary;
		if(Face0.m_Face == nullptr)
			return -Size * 0.2f;
		UpdateMetrics(Face0, Size);
		return Face0.m_Descent;
	}

	float CTextEngine::LineHeight(int Face, float Size)
	{
		CFontFace &Face0 = m_aSlots[std::clamp(Face, 0, 1)].m_Primary;
		if(Face0.m_Face == nullptr)
			return Size * 1.2f;
		UpdateMetrics(Face0, Size);
		return Face0.m_LineHeight;
	}

	void CTextEngine::AllocGlyph(int Width, int Height, int &OutX, int &OutY)
	{
		if(m_ShelfX + Width + AtlasPadding > m_AtlasWidth)
		{
			m_ShelfY += m_ShelfHeight + AtlasPadding;
			m_ShelfX = 0;
			m_ShelfHeight = 0;
		}
		OutX = m_ShelfX;
		OutY = m_ShelfY;
		m_ShelfX += Width + AtlasPadding;
		m_ShelfHeight = std::max(m_ShelfHeight, Height);
	}

	bool CTextEngine::GrowAtlas()
	{
		if(m_AtlasWidth >= MaxAtlasSize)
			return false;
		const int NewSize = std::min(m_AtlasWidth * 2, MaxAtlasSize);
		LOG_TRACE("text", "glyph atlas grows to {:d}", NewSize);
		m_AtlasWidth = NewSize;
		m_AtlasHeight = NewSize;
		// every cached glyph has to be rasterized again
		m_AtlasPixels.assign(static_cast<std::size_t>(m_AtlasWidth) * m_AtlasHeight, std::byte{0});
		m_Glyphs.clear();
		m_ShelfX = 0;
		m_ShelfY = 0;
		m_ShelfHeight = 0;
		m_AtlasRevision++;
		return true;
	}

	// Rasterizes one glyph of one face. Glyph is a real FreeType glyph index:
	// HarfBuzz produced it while shaping, so it must not be looked up again.
	const CGlyph *CTextEngine::Rasterize(int Face, CFontFace *pFace, std::uint32_t Glyph, float Size)
	{
		if(pFace == nullptr || pFace->m_Face == nullptr)
			return nullptr;
		const int SlotIndex = std::clamp(Face, 0, 1);
		// the cache key includes the face the glyph came from, because the same
		// glyph index means different things in different fonts
		const CKey Key{SlotIndex, (static_cast<std::uint32_t>(
			reinterpret_cast<std::uintptr_t>(pFace->m_Face) >> 4) ^ Glyph), QuantiseSize(Size)};
		const auto Found = m_Glyphs.find(Key);
		if(Found != m_Glyphs.end())
			return &Found->second;

		const float RasterSize = UnquantiseSize(Key.m_Size);
		UpdateMetrics(*pFace, RasterSize);

		if(FT_Load_Glyph(pFace->m_Face, Glyph, FT_LOAD_DEFAULT) != 0)
			return nullptr;
		if(FT_Render_Glyph(pFace->m_Face->glyph, FT_RENDER_MODE_NORMAL) != 0)
			return nullptr;

		const FT_Bitmap &Bitmap = pFace->m_Face->glyph->bitmap;
		CGlyph New;
		New.m_Size = RasterSize;
		New.m_Width = static_cast<int>(Bitmap.width);
		New.m_Height = static_cast<int>(Bitmap.rows);
		// the bearings are in pixels, the pen sits on the baseline
		New.m_BearingX = static_cast<float>(pFace->m_Face->glyph->bitmap_left);
		New.m_BearingY = static_cast<float>(pFace->m_Face->glyph->bitmap_top);
		New.m_bValid = New.m_Width > 0 && New.m_Height > 0;

		int Guard = 0;
		while(New.m_bValid)
		{
			AllocGlyph(New.m_Width, New.m_Height, New.m_AtlasX, New.m_AtlasY);
			if(New.m_AtlasY + New.m_Height <= m_AtlasHeight)
				break;
			if(!GrowAtlas() || ++Guard > 4)
			{
				New.m_bValid = false;
				break;
			}
			// GrowAtlas cleared the cache, so nothing may be reused; this glyph
			// is simply re-allocated in the fresh atlas
		}

		if(New.m_bValid)
		{
			for(int y = 0; y < New.m_Height; y++)
			{
				const unsigned char *pSource = Bitmap.buffer + static_cast<std::ptrdiff_t>(y) * Bitmap.pitch;
				std::byte *pTarget = m_AtlasPixels.data() +
					static_cast<std::size_t>(New.m_AtlasY + y) * m_AtlasWidth + New.m_AtlasX;
				// FT_PIXEL_MODE_GRAY is one byte per pixel; anything else is
				// rare enough to be flattened to full coverage
				if(Bitmap.pixel_mode == FT_PIXEL_MODE_GRAY)
					std::memcpy(pTarget, pSource, static_cast<std::size_t>(New.m_Width));
				else
					for(int x = 0; x < New.m_Width; x++)
						pTarget[x] = pSource[x] != 0 ? std::byte{255} : std::byte{0};
			}
			m_AtlasRevision++;
		}

		const auto Inserted = m_Glyphs.emplace(Key, New);
		return &Inserted.first->second;
	}

	float CTextEngine::Layout(int Face, float Size, const std::string &Text, std::vector<CGlyphQuad> *pOut)
	{
		if(Text.empty())
			return 0.0f;
		const int SlotIndex = std::clamp(Face, 0, 1);
		CSlot &Slot = m_aSlots[SlotIndex];

		// the runs are shaped one at a time: a run is a stretch of codepoints
		// that all resolve to the same face, which is what makes the fallback
		// work without a second layout pass
		float PenX = 0.0f;
		std::size_t Index = 0;
		while(Index < Text.size())
		{
			// collect the codepoints of this run
			m_GlyphIds.clear();
			std::vector<std::uint32_t> Codepoints;
			CFontFace *pRunFace = nullptr;
			while(Index < Text.size())
			{
				const std::size_t Before = Index;
				const std::uint32_t Code = NextCodepoint(Text, Index);
				(void)Before;
				CFontFace *pFace = FaceForCodepoint(Slot, Code);
				if(pRunFace == nullptr)
					pRunFace = pFace;
				else if(pFace != pRunFace)
				{
					// this codepoint belongs to the next run; rewind it
					Index = Before;
					break;
				}
				Codepoints.push_back(Code);
				if(Codepoints.size() >= 512)
					break;
			}
			if(pRunFace == nullptr || pRunFace->m_Face == nullptr || Codepoints.empty())
				break;

			UpdateMetrics(*pRunFace, Size);
			// HarfBuzz shapes at a fixed scale; the font size comes from
			// FreeType, so the glyph positions are already in pixels
			hb_font_set_scale(pRunFace->m_pHbFont, static_cast<int>(std::lround(Size * 64.0f)),
				static_cast<int>(std::lround(Size * 64.0f)));
			hb_ft_font_set_load_flags(pRunFace->m_pHbFont, FT_LOAD_DEFAULT);

			hb_buffer_t *pBuffer = hb_buffer_create();
			hb_buffer_add_utf32(pBuffer, Codepoints.data(), static_cast<int>(Codepoints.size()), 0,
				static_cast<int>(Codepoints.size()));
			hb_buffer_set_direction(pBuffer, HB_DIRECTION_LTR);
			hb_buffer_set_script(pBuffer, HB_SCRIPT_LATIN);
			hb_buffer_set_language(pBuffer, hb_language_from_string("en", -1));
			hb_shape(pRunFace->m_pHbFont, pBuffer, nullptr, 0);

			unsigned int Count = 0;
			const hb_glyph_info_t *pInfo = hb_buffer_get_glyph_infos(pBuffer, &Count);
			const hb_glyph_position_t *pPos = hb_buffer_get_glyph_positions(pBuffer, &Count);

			for(unsigned int i = 0; i < Count; i++)
			{
				// HarfBuzz positions are in 26.6 of the scale set above
				const float Advance = static_cast<float>(pPos[i].x_advance) / 64.0f;
				const float OffsetX = static_cast<float>(pPos[i].x_offset) / 64.0f;
				const float OffsetY = static_cast<float>(pPos[i].y_offset) / 64.0f;
				const std::uint32_t GlyphId = pInfo[i].codepoint;

				if(pOut != nullptr && GlyphId != 0)
				{
					if(const CGlyph *pGlyph = Rasterize(SlotIndex, pRunFace, GlyphId, Size))
					{
						if(pGlyph->m_bValid)
						{
							CGlyphQuad Quad;
							Quad.m_X = PenX + OffsetX + pGlyph->m_BearingX;
							// y grows downward on screen, the bearing is up
							Quad.m_Y = -(OffsetY + pGlyph->m_BearingY);
							Quad.m_Width = static_cast<float>(pGlyph->m_Width);
							Quad.m_Height = static_cast<float>(pGlyph->m_Height);
							Quad.m_U0 = static_cast<float>(pGlyph->m_AtlasX);
							Quad.m_V0 = static_cast<float>(pGlyph->m_AtlasY);
							Quad.m_U1 = static_cast<float>(pGlyph->m_AtlasX + pGlyph->m_Width);
							Quad.m_V1 = static_cast<float>(pGlyph->m_AtlasY + pGlyph->m_Height);
							pOut->push_back(Quad);
						}
					}
				}
				PenX += Advance;
			}

			hb_buffer_destroy(pBuffer);
		}
		return PenX;
	}
} // namespace engine
