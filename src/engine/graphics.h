#ifndef ENGINE_GRAPHICS_H
#define ENGINE_GRAPHICS_H

/*
	An SDL3 window with the SDL3 renderer, and the painter on top of it.

	Textures are cached by name; the font is the system default sans serif,
	found through fontconfig, with a CJK fallback so the interface can show
	Chinese labels as well.

	The renderer is chosen by name rather than letting SDL pick, because the
	editor is happiest with an accelerated backend: "opengl" is what every
	build ships, and the software renderer is the last resort.
*/

#include <string>
#include <vector>

#include <SDL3/SDL_events.h>
#include <sugar_lazytable.h>
#include <sugar_types.h>

#include "base/math.h"
#include "base/storage.h"
#include "engine/input.h"
#include "engine/painter.h"
#include "engine/text.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace config
{
	class CConfig;
}

namespace engine
{
	class CGraphics
	{
	public:
		~CGraphics();

		// pConfig is optional; its font overrides win over the platform lookup
		bool Init(const std::string &Title, int Width, int Height, bool Visible = true, const config::CConfig *pConfig = nullptr);
		void Close();

		bool ShouldQuit() const;
		void RequestQuit();

		// a frame: the editor draws between BeginFrame and EndFrame, EndFrame
		// presents (unless it is a screenshot)
		void BeginFrame(bool PumpEvents = true);
		void EndFrame(bool Present = true);

		painter::CPainter *Painter() { return &m_Painter; }
		CTextEngine &Text() { return m_Text; }
		SDL_Window *Window() const { return m_pWindow; }
		SDL_Renderer *Renderer() const { return m_pRenderer; }
		CInput &Input() { return m_Input; }
		const CInput &Input() const { return m_Input; }

		// the logical (window) size, which is what the layout works in, and the
		// pixel size of the drawable; they only differ when the display is
		// scaled
		base::vec2 WindowSize() const { return m_WindowSize; }
		base::vec2 FramebufferSize() const { return m_FramebufferSize; }
		float PixelRatio() const { return m_PixelRatio; }

		// the colour the frame is cleared to before the editor draws over it
		void SetClearColor(float Red, float Green, float Blue);

		void SetWindowTitle(const std::string &Title);
		// resizes the window, which is what the screenshot mode uses to pick a
		// deterministic size and what a test can use to check the layout
		void SetWindowSize(int Width, int Height);

		// ---------------------------------------------------------- textures
		// -1 when the key is unknown; LoadTextureFile returns -1 on failure
		int Texture(const std::string &Key) const;
		int LoadTextureFile(const std::string &Key, const storage::path &File);
		int CreateTextureRGBA(const std::string &Key, int Width, int Height, const storage::byte_vector &Pixels);
		// replaces the pixels of an existing texture of the same size; returns
		// false when there is nothing to update, so the caller creates it instead
		bool UpdateTextureRGBA(const std::string &Key, int Width, int Height, const storage::byte_vector &Pixels);
		// drops one texture, for a caller that reuses a key for another image
		void ForgetTexture(const std::string &Key);
		void ForgetTextures();

		// the pixel size of a texture, which the tile painter needs to turn a
		// tile index into a texel window
		bool TextureSize(int Texture, int &Width, int &Height) const;

		// ------------------------------------------------------------- fonts
		int Font() const { return m_Font; }
		int FontBold() const { return m_FontBold; }
		float TextWidth(float Size, const std::string &Text, bool Bold = false) const;
		// the paths that were used, for the about panel
		const std::string &FontPath() const { return m_FontPath; }

		// hands the painter a way to reach the textures it was told about
		static SDL_Texture *LookupTextureCallback(void *pUser, int Texture);

	private:
		void HandleEvent(const SDL_Event &Event);

		SDL_Window *m_pWindow = nullptr;
		SDL_Renderer *m_pRenderer = nullptr;
		CTextEngine m_Text;
		painter::CPainter m_Painter;
		CInput m_Input;

		base::vec2 m_WindowSize;
		base::vec2 m_FramebufferSize;
		float m_PixelRatio = 1.0f;
		bool m_bShouldQuit = false;
		// the editor's panel background, so a resize never shows uninitialised
		// pixels; ui::Theme owns the value and hands it over in Init
		float m_ClearColor[3] = {24.0f / 255.0f, 26.0f / 255.0f, 30.0f / 255.0f};

		// The texture cache. The rest of the interface refers to a texture by a
		// small positive int, so the SDL texture is held in a table and the
		// handle is an index into it; a pointer cannot be smuggled through an
		// int, it would be truncated on a 64 bit machine.
		libsugarx::lazy_flat_table<std::string, int> m_Textures;
		std::vector<SDL_Texture *> m_vTextureHandles;
		// the size of each cached texture, for the tile math
		libsugarx::lazy_flat_table<std::string, base::vec2> m_TextureSizes;

		// stores the texture and returns the handle the interface will use
		int RegisterTexture(const std::string &Key, SDL_Texture *pTexture, base::vec2 Size);

		int m_Font = 0;
		int m_FontBold = 1;
		std::string m_FontPath;
	};
} // namespace engine

#endif // ENGINE_GRAPHICS_H
