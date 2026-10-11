#ifndef ENGINE_VECTOR_TEXTURES_H
#define ENGINE_VECTOR_TEXTURES_H

/*
	The vector texture cache: an SVG document behind every texture, rasterized
	at the pixel size the display actually needs.

	A bitmap icon has one size and is magnified on a denser display, which is
	exactly the softness vector art is supposed to avoid. Here the drawing is
	parsed once and rasterized again whenever the size it is drawn at changes,
	so an eighteen pixel icon becomes an eighteen pixel texture on an ordinary
	display, a thirty-six pixel one on a display with twice the density, and
	twice that again if the user asks for a bigger interface. The display scale
	is read from CGraphics, so a window that moves to another monitor - or a
	densely scaled display that SDL reports at start up - re-rasterizes without
	anything else having to notice.

	The textures live in the graphics cache under a key of their own, and they
	are replaced *in place*: the handle a panel is holding stays valid, which is
	what makes "the same icon, now at another size" a non-event for the caller.

	Like the rest of the drawing layer this is window build only; the SVG
	parsing and rasterizing it is built on (engine/svg.h) is not, so the
	selftest checks the icons without a renderer.
*/

#include <map>
#include <string>

#include <sugar_types.h>

#include "base/math.h"
#include "base/storage.h"
#include "engine/graphics.h"
#include "engine/svg.h"

namespace engine
{
	class CVectorTextures
	{
	public:
		void SetGraphics(CGraphics *pGraphics) { m_pGraphics = pGraphics; }

		// registers a document under a name; registering it again re-parses it
		bool Add(const std::string &Name, const storage::path &File, bool bWhite = true);
		/*
			Drops the handles of the textures it made without forgetting the
			documents. It is what a map switch needs: the graphics cache throws
			every texture away when another map is loaded, so a handle kept here
			would point at nothing and the icons would quietly stop drawing.
		*/
		void Forget();
		// forgets everything, including the textures it made
		void Close();
		// re-parses every document from the file it came from
		void Reload();

		/*
			The texture for Name, rasterized for a drawing that occupies
			LogicalSize pixels on screen, and the pixel size it was rasterized
			at - which is what the painter needs to sample it. -1 when the name
			is unknown or the document failed to rasterize; the caller draws
			nothing in that case, which is better than a wrong picture.
		*/
		int Texture(const std::string &Name, float LogicalSize, int &PixelSize);
		int Texture(const std::string &Name, float LogicalSize)
		{
			int Size = 0;
			return Texture(Name, LogicalSize, Size);
		}

		/*
			The scale every texture is rasterized at: the display density times
			the interface scale. A change invalidates what was rasterized, and
			the next request for each texture re-rasterizes it right there.
		*/
		void SetDisplayScale(float Scale);
		float DisplayScale() const { return m_Scale; }

		// how many rasterizations have run, which is what tells "the textures
		// follow the display" apart from "they were made once at start up"
		int Rasterized() const { return m_Rasterized; }
		int Count() const { return static_cast<int>(m_Entries.size()); }
		// the pixel size a name was last rasterized at; false when unknown
		bool RasterizedSize(const std::string &Name, int &Width, int &Height) const;

	private:
		struct CEntry
		{
			svg::CDocument m_Document;
			storage::path m_File;
			bool m_bWhite = true;
			std::string m_Key;
			int m_Width = 0;
			int m_Height = 0;
			int m_Texture = -1;
		};

		// the entry for a name, created on first use; nullptr when there is none
		CEntry *Find(const std::string &Name);

		CGraphics *m_pGraphics = nullptr;
		std::map<std::string, CEntry> m_Entries;
		float m_Scale = 1.0f;
		int m_Rasterized = 0;
	};
} // namespace engine

#endif // ENGINE_VECTOR_TEXTURES_H
