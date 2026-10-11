/*
	The texture side of the editor: the tile sheets and quads are drawn from
	textures, and only the windowed build has a renderer to upload them to.

	Everything in this file touches CGraphics, which is why it lives apart from
	editor.cpp: the editor *model* is linked into the headless command line and
	the agent interface, where there is no renderer at all, and the linker must
	not be asked for one.
*/

#include <algorithm>
#include <cmath>
#include <map>

#include "base/string.h"
#include "engine/graphics.h"
#include "game/assets.h"
#include "game/editor/editor.h"

#if UNITEED_GUI

namespace game
{

	int CEditor::TextureForImage(int ImageIndex)
	{
		if(ImageIndex < 0 || ImageIndex >= static_cast<int>(m_Map.m_vImages.size()))
			return -1;
		const CImage &Image = m_Map.m_vImages[static_cast<std::size_t>(ImageIndex)];

		if(Image.IsEmbedded())
			return m_pGraphics->CreateTextureRGBA("embedded:" + Image.m_Name, Image.m_Width, Image.m_Height, Image.m_Data);

		// a package may carry the mapres file, that wins over the local one
		for(const CUnitwpResource &Resource : m_Package.m_vResources)
		{
			if(Resource.m_Name != "mapres/" + Image.m_Name + ".png")
				continue;
			const int Cached = m_pGraphics->Texture("res:" + Resource.m_Name);
			if(Cached != -1)
				return Cached;
			const std::optional<CDecodedImage> Decoded = DecodePng(Resource.m_Data);
			if(!Decoded.has_value())
				return -1;
			return m_pGraphics->CreateTextureRGBA("res:" + Resource.m_Name, Decoded->m_Width, Decoded->m_Height, Decoded->m_Pixels);
		}

		const std::optional<storage::path> File = FindImageFile(Image.m_Name);
		if(!File.has_value())
			return -1;
		return m_pGraphics->LoadTextureFile("mapres:" + Image.m_Name, *File);
	}

	// ----------------------------------------------------------------- colours
	namespace
	{
		int TintChannel(int Value, float Scale)
		{
			return base::clamp(static_cast<int>(std::lround(static_cast<float>(Value) * Scale)), 0, 255);
		}
	} // namespace

	/*
		The layer colour multiplies the texture, exactly like the reference does
		it with vertex colours. An image fill can only be faded by a
		scalar alpha, so a coloured tint needs a copy of the image whose rgb is
		already multiplied; those copies are kept in a small, fixed set of slots
		per image, because a colour envelope animates and an unbounded texture
		cache would grow with every frame of it.

		Animated alpha (the common case, a fading layer) uses the pattern alpha
		and costs nothing; only an animated *rgb* needs its slot rewritten, which
		is a tint and one texture upload per frame.
	*/
	int CEditor::TextureForImage(int ImageIndex, const CColor &Tint)
	{
		if(Tint.r >= 255 && Tint.g >= 255 && Tint.b >= 255)
			return TextureForImage(ImageIndex);
		if(ImageIndex < 0 || ImageIndex >= static_cast<int>(m_Map.m_vImages.size()))
			return -1;

		m_TintClock++;

		// an exact hit first: the static colour of a layer stays in its slot
		int Slot = -1;
		std::size_t PerImage = 0;
		std::size_t LeastUsed = 0;
		unsigned LeastUsedClock = 0;
		bool bHasLeastUsed = false;
		for(std::size_t i = 0; i < m_vTintSlots.size(); i++)
		{
			CTintSlot &Candidate = m_vTintSlots[i];
			if(Candidate.m_Image == ImageIndex && Candidate.m_Tint.r == Tint.r && Candidate.m_Tint.g == Tint.g && Candidate.m_Tint.b == Tint.b)
			{
				Candidate.m_LastUse = m_TintClock;
				return m_pGraphics->Texture(TintSlotKey(i));
			}
			if(Candidate.m_Image == ImageIndex)
			{
				PerImage++;
				if(!bHasLeastUsed || Candidate.m_LastUse < LeastUsedClock)
				{
					bHasLeastUsed = true;
					LeastUsedClock = Candidate.m_LastUse;
					LeastUsed = i;
				}
			}
		}

		// then a free slot, then the oldest slot of this image, and only if this
		// image has fewer than the cap one more slot
		for(std::size_t i = 0; i < m_vTintSlots.size() && Slot < 0; i++)
			if(m_vTintSlots[i].m_Image < 0)
				Slot = static_cast<int>(i);
		if(Slot < 0 && PerImage < MaxTintSlotsPerImage)
		{
			m_vTintSlots.push_back(CTintSlot{});
			Slot = static_cast<int>(m_vTintSlots.size()) - 1;
		}
		if(Slot < 0 && bHasLeastUsed)
			Slot = static_cast<int>(LeastUsed);
		if(Slot < 0)
			return TextureForImage(ImageIndex); // out of slots, show it untinted

		const CImage &Image = m_Map.m_vImages[static_cast<std::size_t>(ImageIndex)];
		int SourceWidth = Image.m_Width;
		int SourceHeight = Image.m_Height;
		std::vector<std::byte> Source;
		if(Image.IsEmbedded())
		{
			if(Image.m_Data.empty() || SourceWidth <= 0 || SourceHeight <= 0)
				return -1;
			Source = Image.m_Data;
		}
		else
		{
			if(!TintSource(ImageIndex, Source, SourceWidth, SourceHeight))
				return -1;
		}

		const std::size_t Count = static_cast<std::size_t>(SourceWidth) * static_cast<std::size_t>(SourceHeight);
		if(Source.size() < Count * 4)
			return -1;

		const float Red = static_cast<float>(Tint.r) / 255.0f;
		const float Green = static_cast<float>(Tint.g) / 255.0f;
		const float Blue = static_cast<float>(Tint.b) / 255.0f;
		storage::byte_vector Tinted(Source.size());
		for(std::size_t i = 0; i < Count; i++)
		{
			const std::size_t At = i * 4;
			Tinted[At + 0] = static_cast<std::byte>(TintChannel(static_cast<unsigned char>(Source[At + 0]), Red));
			Tinted[At + 1] = static_cast<std::byte>(TintChannel(static_cast<unsigned char>(Source[At + 1]), Green));
			Tinted[At + 2] = static_cast<std::byte>(TintChannel(static_cast<unsigned char>(Source[At + 2]), Blue));
			Tinted[At + 3] = Source[At + 3]; // the alpha comes from the pattern
		}

		CTintSlot &Chosen = m_vTintSlots[static_cast<std::size_t>(Slot)];
		const std::string Key = TintSlotKey(static_cast<std::size_t>(Slot));
		int Handle = -1;
		if(Chosen.m_Image == ImageIndex && Chosen.m_Width == SourceWidth && Chosen.m_Height == SourceHeight)
			Handle = m_pGraphics->UpdateTextureRGBA(Key, SourceWidth, SourceHeight, Tinted) ? m_pGraphics->Texture(Key) : -1;
		else
			m_pGraphics->ForgetTexture(Key); // the slot changes image or size
		if(Handle == -1)
			Handle = m_pGraphics->CreateTextureRGBA(Key, SourceWidth, SourceHeight, Tinted);
		if(Handle == -1)
			return TextureForImage(ImageIndex);

		Chosen.m_Image = ImageIndex;
		Chosen.m_Tint = Tint;
		Chosen.m_Width = SourceWidth;
		Chosen.m_Height = SourceHeight;
		Chosen.m_LastUse = m_TintClock;
		return Handle;
	}

	// the decoded pixels of a mapres file, kept because a tint rewrite should
	// not decode the png again
	bool CEditor::TintSource(int ImageIndex, std::vector<std::byte> &Pixels, int &Width, int &Height)
	{
		if(ImageIndex < 0 || ImageIndex >= static_cast<int>(m_Map.m_vImages.size()))
			return false;
		const CImage &Image = m_Map.m_vImages[static_cast<std::size_t>(ImageIndex)];

		const auto Cached = m_SourceImages.find(ImageIndex);
		if(Cached != m_SourceImages.end())
		{
			Pixels = Cached->second.m_Pixels;
			Width = Cached->second.m_Width;
			Height = Cached->second.m_Height;
			return !Pixels.empty();
		}

		const std::optional<storage::path> File = FindImageFile(Image.m_Name);
		if(!File.has_value())
			return false;
		const std::optional<storage::byte_vector> Bytes = storage::read(*File);
		if(!Bytes.has_value())
			return false;
		const std::optional<CDecodedImage> Decoded = DecodePng(*Bytes);
		if(!Decoded.has_value())
			return false;

		CSourceImage Stored;
		Stored.m_Width = Decoded->m_Width;
		Stored.m_Height = Decoded->m_Height;
		Stored.m_Pixels = Decoded->m_Pixels;
		const auto Inserted = m_SourceImages.emplace(ImageIndex, std::move(Stored));
		Pixels = Inserted.first->second.m_Pixels;
		Width = Inserted.first->second.m_Width;
		Height = Inserted.first->second.m_Height;
		return !Pixels.empty();
	}

	std::string CEditor::TintSlotKey(std::size_t Slot) const
	{
		return base::Format("tintslot:%zu", Slot);
	}

} // namespace game

#else

namespace game
{
	// The headless build has no renderer, so a texture is always absent. Every
	// caller already treats -1 as "nothing to draw", which is why the stub is
	// enough and no drawing path has to know which build it is in.
	int CEditor::TextureForImage(int ImageIndex)
	{
		(void)ImageIndex;
		return -1;
	}

	int CEditor::TextureForImage(int ImageIndex, const CColor &Tint)
	{
		(void)ImageIndex;
		(void)Tint;
		return -1;
	}

	bool CEditor::TintSource(int ImageIndex, std::vector<std::byte> &Pixels, int &Width, int &Height)
	{
		(void)ImageIndex;
		(void)Pixels;
		(void)Width;
		(void)Height;
		return false;
	}

	std::string CEditor::TintSlotKey(std::size_t Slot) const { return base::Format("tintslot:%zu", Slot); }

} // namespace game

#endif
