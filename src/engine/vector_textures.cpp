#include <algorithm>
#include <cmath>

#include "base/logger.h"
#include "engine/vector_textures.h"

namespace engine
{
	bool CVectorTextures::Add(const std::string &Name, const storage::path &File, bool bWhite)
	{
		CEntry &Entry = m_Entries[Name];
		Entry.m_File = File;
		Entry.m_bWhite = bWhite;
		// the graphics key is the name: one icon, one texture, whatever size
		// it currently has
		Entry.m_Key = "svg:" + Name;
		// the size is from the last rasterization, so a re-added document has
		// to be re-rasterized before it is drawn again
		Entry.m_Width = 0;
		Entry.m_Height = 0;
		Entry.m_Texture = -1;

		if(!Entry.m_Document.LoadFromFile(File))
		{
			LOG_WARN("vector", "the document '%s' could not be loaded", File.string().c_str());
			return false;
		}
		if(bWhite)
			Entry.m_Document.MakeWhite();
		return true;
	}

	void CVectorTextures::Forget()
	{
		int Forgotten = 0;
		for(auto &Pair : m_Entries)
		{
			CEntry &Entry = Pair.second;
			if(Entry.m_Texture <= 0)
				continue;
			// the graphics may have destroyed it already, in which case this is
			// only the bookkeeping it was going to do anyway
			if(m_pGraphics != nullptr && !Entry.m_Key.empty())
				m_pGraphics->ForgetTexture(Entry.m_Key);
			Entry.m_Texture = -1;
			Entry.m_Width = 0;
			Entry.m_Height = 0;
			Forgotten++;
		}
		if(Forgotten > 0)
			LOG_TRACE("vector", "%d icons will be rasterized again", Forgotten);
	}

	void CVectorTextures::Close()
	{
		for(auto &Pair : m_Entries)
		{
			// the graphics cache owns the SDL texture; dropping the handle here
			// is enough, and the key is forgotten with it
			if(m_pGraphics != nullptr && !Pair.second.m_Key.empty())
				m_pGraphics->ForgetTexture(Pair.second.m_Key);
		}
		m_Entries.clear();
		m_Rasterized = 0;
	}

	void CVectorTextures::Reload()
	{
		// the documents are re-parsed from the files they came from, so an edit
		// to an icon shows up without restarting
		for(auto &Pair : m_Entries)
		{
			CEntry &Entry = Pair.second;
			if(!Entry.m_Document.LoadFromFile(Entry.m_File))
				continue;
			if(Entry.m_bWhite)
				Entry.m_Document.MakeWhite();
			// the size of the drawing may have changed with it
			Entry.m_Width = 0;
			Entry.m_Height = 0;
		}
	}

	void CVectorTextures::SetDisplayScale(float Scale)
	{
		if(Scale <= 0.0f || std::abs(Scale - m_Scale) < 0.01f)
			return;
		// a fractional display scale can wobble by a hundredth between frames
		// without changing any texture, and that is not worth reporting
		const int Before = svg::RasterSize(18.0f, m_Scale);
		m_Scale = Scale;
		const int After = svg::RasterSize(18.0f, m_Scale);
		if(Before == After)
			return;
		/*
			Nothing is thrown away here: every texture notices at its next
			request that the size it was rasterized at is no longer the size it
			is wanted at, and re-rasterizes then. An icon that is not on screen
			therefore costs nothing.
		*/
		LOG_INFO("vector", "display scale %.2f: the %d vector textures follow it (an 18 pixel icon becomes %d)",
			static_cast<double>(m_Scale), Count(), After);
	}

	CVectorTextures::CEntry *CVectorTextures::Find(const std::string &Name)
	{
		const auto It = m_Entries.find(Name);
		return It == m_Entries.end() ? nullptr : &It->second;
	}

	int CVectorTextures::Texture(const std::string &Name, float LogicalSize, int &PixelSize)
	{
		PixelSize = 0;
		CEntry *pEntry = Find(Name);
		if(pEntry == nullptr || m_pGraphics == nullptr || !pEntry->m_Document.Loaded())
			return -1;

		const int Size = svg::RasterSize(LogicalSize, m_Scale);
		if(pEntry->m_Texture > 0 && pEntry->m_Width == Size && pEntry->m_Height == Size)
		{
			PixelSize = Size;
			return pEntry->m_Texture;
		}

		storage::byte_vector Pixels;
		if(!pEntry->m_Document.Rasterize(Size, Size, Pixels))
			return -1;

		// linear sampling: the icon is rasterized for the size it is drawn at,
		// but the layout rounds to fractions of a pixel and a vector drawing
		// should not show that as a hard edge
		pEntry->m_Texture = m_pGraphics->ReplaceTextureRGBA(pEntry->m_Key, Size, Size, Pixels, true);
		if(pEntry->m_Width > 0 && pEntry->m_Width != Size)
		{
			// the display changed under a drawing that was already made: this
			// is the line that says the icons followed it
			LOG_INFO("vector", "'%s' re-rasterized at %dx%d (was %dx%d)", Name.c_str(), Size, Size,
				pEntry->m_Width, pEntry->m_Height);
		}
		else
			LOG_TRACE("vector", "'%s' rasterized at %dx%d (scale %.2f)", Name.c_str(), Size, Size,
				static_cast<double>(m_Scale));
		pEntry->m_Width = Size;
		pEntry->m_Height = Size;
		m_Rasterized++;
		PixelSize = Size;
		return pEntry->m_Texture;
	}

	bool CVectorTextures::RasterizedSize(const std::string &Name, int &Width, int &Height) const
	{
		const auto It = m_Entries.find(Name);
		if(It == m_Entries.end() || It->second.m_Width <= 0)
			return false;
		Width = It->second.m_Width;
		Height = It->second.m_Height;
		return true;
	}
} // namespace engine
