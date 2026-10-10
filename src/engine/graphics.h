#ifndef ENGINE_GRAPHICS_H
#define ENGINE_GRAPHICS_H

/*
	A GLFW window with a nanovg renderer, the same lightweight stack SandboxCmd
	uses (glfw3, OpenGL, nanovg, glad). Textures are cached by name; the font is
	the system default sans serif, found through fontconfig, with a CJK fallback
	so the interface can show Chinese labels as well.
*/

#include <string>
#include <vector>

#include <sugar_lazytable.h>
#include <sugar_types.h>

#include "base/math.h"
#include "base/storage.h"
#include "engine/input.h"

struct NVGcontext;
struct GLFWwindow;

namespace engine
{
	class CGraphics
	{
	public:
		~CGraphics();

		bool Init(const std::string &Title, int Width, int Height, bool Visible = true);
		void Close();

		bool ShouldQuit() const;
		void RequestQuit();

		// a frame: the editor draws between BeginFrame and EndFrame, EndFrame
		// swaps the buffers and pumps the events (unless it is a screenshot)
		void BeginFrame(bool PumpEvents = true);
		void EndFrame(bool SwapBuffers = true);

		NVGcontext *Context() const { return m_pContext; }
		GLFWwindow *Window() const { return m_pWindow; }
		CInput &Input() { return m_Input; }
		const CInput &Input() const { return m_Input; }

		base::vec2 WindowSize() const { return m_WindowSize; }
		float PixelRatio() const { return m_PixelRatio; }

		void SetWindowTitle(const std::string &Title);

		// ---------------------------------------------------------- textures
		// -1 when the key is unknown; LoadTextureFile returns -1 on failure
		int Texture(const std::string &Key) const;
		int LoadTextureFile(const std::string &Key, const storage::path &File);
		int CreateTextureRGBA(const std::string &Key, int Width, int Height, const storage::byte_vector &Pixels);
		void ForgetTextures();

		// ------------------------------------------------------------- fonts
		int Font() const { return m_Font; }
		int FontBold() const { return m_FontBold; }
		float TextWidth(float Size, const std::string &Text, bool Bold = false) const;
		// the paths that were used, for the about panel
		const std::string &FontPath() const { return m_FontPath; }

	private:
		GLFWwindow *m_pWindow = nullptr;
		NVGcontext *m_pContext = nullptr;
		CInput m_Input;

		base::vec2 m_WindowSize;
		float m_PixelRatio = 1.0f;

		libsugarx::lazy_flat_table<std::string, int> m_Textures;
		int m_Font = 0;
		int m_FontBold = 0;
		std::string m_FontPath;
	};
} // namespace engine

#endif // ENGINE_GRAPHICS_H
