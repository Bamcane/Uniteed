#ifndef ENGINE_SVG_H
#define ENGINE_SVG_H

/*
	Scalable vector graphics, through nanosvg.

	Only as much as the interface needs: parse a document once, keep it, and
	rasterize it again at whatever pixel size the display asks for. That is the
	whole point of shipping the icons as vectors rather than bitmaps - a 24 unit
	drawing becomes a 24 pixel texture on an ordinary display and a 48 pixel one
	on a display with twice the density, instead of one small bitmap that has to
	be magnified and goes soft.

	nanosvg is header only and pure C, so this part builds in the headless
	configuration as well: the selftest rasterizes the bundled icons, which
	proves the parser and the rasterizer work on a machine with no window.

	Two things the caller has to know:

	- nsvgParse() rewrites the string it is given (its XML parser terminates
	  attribute values in place), which is why a document owns a mutable copy of
	  its source rather than pointing at the caller's.
	- The rasterizer writes RGBA with the alpha *not* premultiplied, which is
	  what the renderer wants for ABGR8888 with blending on.
*/

#include <cstddef>
#include <string>

#include <sugar_types.h>

#include "base/math.h"
#include "base/storage.h"

struct NSVGimage;

namespace engine::svg
{
	class CDocument
	{
	public:
		CDocument() = default;
		~CDocument();

		// the parsed image is owned here, so a document is only movable
		CDocument(const CDocument &) = delete;
		CDocument &operator=(const CDocument &) = delete;
		CDocument(CDocument &&Other) noexcept;
		CDocument &operator=(CDocument &&Other) noexcept;

		bool LoadFromMemory(std::string Source, const char *pUnits = "px", float Dpi = 96.0f);
		bool LoadFromFile(const storage::path &File, const char *pUnits = "px", float Dpi = 96.0f);
		void Close();

		bool Loaded() const { return m_pImage != nullptr; }
		// the size the document declares, in its own units
		base::vec2 Size() const;
		const std::string &Source() const { return m_Source; }

		/*
			Rasterizes into Width x Height RGBA: the drawing is scaled to fit
			with its aspect kept and centred, and the buffer is resized to
			Width * Height * 4.
		*/
		bool Rasterize(int Width, int Height, storage::byte_vector &Pixels);

		/*
			Paints every shape white and keeps its alpha, so the colour of an
			icon comes from the painter's tint - which is what lets one texture
			serve the normal, the hover and the disabled state.
		*/
		void MakeWhite();

	private:
		NSVGimage *m_pImage = nullptr;
		std::string m_Source;
	};

	/*
		The pixel size a drawing is rasterized at: what the layout asks for
		times the scale of the display, rounded, and held to a sane range so a
		nonsense scale cannot ask for a gigabyte of texture.
	*/
	int RasterSize(float LogicalSize, float Scale);
} // namespace engine::svg

#endif // ENGINE_SVG_H
