#include "graphics.h"

#include <cstdio>
#include <cstring>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <nanovg.h>
#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg_gl.h>

#include "base/logger.h"

namespace engine
{
	namespace
	{
		CGraphics *g_pGraphics = nullptr;
		int g_WindowWidth = 1280;
		int g_WindowHeight = 800;

		void GlfwErrorCallback(int Error, const char *pDescription)
		{
			LOG_ERROR("glfw", "error %d: %s", Error, pDescription);
		}

		void KeyCallback(GLFWwindow *pWindow, int Key, int Scancode, int Action, int Mods)
		{
			(void)pWindow;
			if(g_pGraphics != nullptr)
				g_pGraphics->Input().OnKey(Key, Scancode, Action, Mods);
		}

		void CharCallback(GLFWwindow *pWindow, unsigned int Codepoint)
		{
			(void)pWindow;
			if(g_pGraphics != nullptr)
				g_pGraphics->Input().OnChar(Codepoint);
		}

		void MouseButtonCallback(GLFWwindow *pWindow, int Button, int Action, int Mods)
		{
			(void)pWindow;
			if(g_pGraphics != nullptr)
				g_pGraphics->Input().OnMouseButton(Button, Action, Mods);
		}

		void CursorPosCallback(GLFWwindow *pWindow, double X, double Y)
		{
			(void)pWindow;
			if(g_pGraphics != nullptr)
				g_pGraphics->Input().OnCursorPos(X, Y);
		}

		void ScrollCallback(GLFWwindow *pWindow, double XOffset, double YOffset)
		{
			(void)pWindow;
			if(g_pGraphics != nullptr)
				g_pGraphics->Input().OnScroll(XOffset, YOffset);
		}

		// the window lost the focus, a key could stay stuck otherwise
		void FocusCallback(GLFWwindow *pWindow, int Focused)
		{
			(void)pWindow;
			if(g_pGraphics != nullptr && Focused == GLFW_FALSE)
				g_pGraphics->Input().OnFocusLost();
		}

		// asks fontconfig for the file of a font pattern, with a plain path
		// fallback list when fontconfig is not installed
		std::string MatchFont(const char *pPattern, const std::vector<std::string> &Fallbacks)
		{
			std::string Command = std::string("fc-match -f '%{file}' '") + pPattern + "' 2>/dev/null";
			if(std::FILE *pPipe = popen(Command.c_str(), "r"))
			{
				char aBuffer[1024] = {0};
				const std::size_t Read = std::fread(aBuffer, 1, sizeof(aBuffer) - 1, pPipe);
				pclose(pPipe);
				if(Read > 0)
				{
					std::string Path(aBuffer, Read);
					while(!Path.empty() && (Path.back() == '\n' || Path.back() == '\r'))
						Path.pop_back();
					if(!Path.empty() && storage::is_file(Path))
						return Path;
				}
			}
			for(const std::string &Path : Fallbacks)
				if(storage::is_file(Path))
					return Path;
			return {};
		}

		bool FontSupportsFallback(const std::string &Path)
		{
			// fontstash cannot seek inside a TrueType collection
			return !base::EndsWith(base::ToLower(Path), ".ttc");
		}
	} // namespace

	CGraphics::~CGraphics() { Close(); }

	bool CGraphics::Init(const std::string &Title, int Width, int Height, bool Visible)
	{
		g_pGraphics = this;
		// the editor may ask for the window size before the first frame
		m_WindowSize = base::vec2(static_cast<float>(Width), static_cast<float>(Height));

		glfwSetErrorCallback(GlfwErrorCallback);
		if(glfwInit() == GLFW_FALSE)
		{
			LOG_ERROR("graphics", "failed to initialise GLFW");
			return false;
		}

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
		glfwWindowHint(GLFW_DEPTH_BITS, 0);
		glfwWindowHint(GLFW_STENCIL_BITS, 8);
		glfwWindowHint(GLFW_SAMPLES, 0);
		glfwWindowHint(GLFW_VISIBLE, Visible ? GLFW_TRUE : GLFW_FALSE);

		m_pWindow = glfwCreateWindow(Width, Height, Title.c_str(), nullptr, nullptr);
		if(m_pWindow == nullptr)
		{
			LOG_ERROR("graphics", "failed to create a window with an OpenGL 3.3 core context");
			glfwTerminate();
			return false;
		}

		glfwMakeContextCurrent(m_pWindow);
		glfwSwapInterval(1);

		if(gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)) == 0)
		{
			LOG_ERROR("graphics", "failed to load the OpenGL functions");
			Close();
			return false;
		}
		LOG_INFO("graphics", "OpenGL %s (%s)", reinterpret_cast<const char *>(glGetString(GL_VERSION)),
			reinterpret_cast<const char *>(glGetString(GL_RENDERER)));

		m_pContext = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
		if(m_pContext == nullptr)
		{
			LOG_ERROR("graphics", "failed to create the nanovg context");
			Close();
			return false;
		}

		// the system default sans serif, plus a CJK fallback for the labels
		const std::string FontPath = MatchFont("sans", {
			"/usr/share/fonts/noto/NotoSans-Regular.ttf",
			"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
			"/usr/share/fonts/TTF/DejaVuSans.ttf",
			"/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
			"/usr/share/fonts/TTF/Hack-Regular.ttf",
		});
		if(!FontPath.empty())
		{
			m_Font = nvgCreateFont(m_pContext, "sans", FontPath.c_str());
			m_FontPath = FontPath;
		}
		if(m_Font < 0)
		{
			LOG_WARN("graphics", "no usable system font found, falling back to nanovg's built in font");
			m_Font = nvgCreateFont(m_pContext, "sans", "");
			m_FontPath.clear();
		}

		const std::string BoldPath = MatchFont("sans:bold", {
			"/usr/share/fonts/noto/NotoSans-Bold.ttf",
			"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
			"/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
			"/usr/share/fonts/TTF/Hack-Bold.ttf",
		});
		m_FontBold = (!BoldPath.empty()) ? nvgCreateFont(m_pContext, "sans-bold", BoldPath.c_str()) : -1;
		if(m_FontBold < 0)
			m_FontBold = m_Font;

		const std::string CjkPath = MatchFont("sans:lang=zh-cn", {
			"/usr/share/fonts/noto/NotoSansCJK-Regular.ttc",
			"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
		});
		if(!CjkPath.empty() && FontSupportsFallback(CjkPath))
		{
			const int Cjk = nvgCreateFont(m_pContext, "sans-cjk", CjkPath.c_str());
			if(Cjk >= 0)
			{
				nvgAddFallbackFontId(m_pContext, m_Font, Cjk);
				if(m_FontBold != m_Font)
					nvgAddFallbackFontId(m_pContext, m_FontBold, Cjk);
				LOG_INFO("graphics", "CJK fallback font: %s", CjkPath.c_str());
			}
		}

		glfwSetKeyCallback(m_pWindow, KeyCallback);
		glfwSetCharCallback(m_pWindow, CharCallback);
		glfwSetMouseButtonCallback(m_pWindow, MouseButtonCallback);
		glfwSetCursorPosCallback(m_pWindow, CursorPosCallback);
		glfwSetScrollCallback(m_pWindow, ScrollCallback);
		glfwSetWindowFocusCallback(m_pWindow, FocusCallback);

		LOG_INFO("graphics", "window created, font '%s'", m_FontPath.empty() ? "(built in)" : m_FontPath.c_str());
		return true;
	}

	void CGraphics::Close()
	{
		ForgetTextures();
		if(m_pContext != nullptr)
		{
			nvgDeleteGL3(m_pContext);
			m_pContext = nullptr;
		}
		if(m_pWindow != nullptr)
		{
			glfwDestroyWindow(m_pWindow);
			m_pWindow = nullptr;
			glfwTerminate();
		}
		g_pGraphics = nullptr;
	}

	bool CGraphics::ShouldQuit() const { return m_pWindow == nullptr || glfwWindowShouldClose(m_pWindow) == GLFW_TRUE; }
	void CGraphics::RequestQuit()
	{
		if(m_pWindow != nullptr)
			glfwSetWindowShouldClose(m_pWindow, GLFW_TRUE);
	}

	void CGraphics::BeginFrame(bool PumpEvents)
	{
		// the per frame buffers are cleared before the events arrive, so the
		// state read below is exactly this frame's input
		m_Input.NewFrame();
		if(PumpEvents)
			glfwPollEvents();
		glfwGetWindowSize(m_pWindow, &g_WindowWidth, &g_WindowHeight);
		int FramebufferWidth = 0, FramebufferHeight = 0;
		glfwGetFramebufferSize(m_pWindow, &FramebufferWidth, &FramebufferHeight);
		m_PixelRatio = (g_WindowWidth > 0) ? static_cast<float>(FramebufferWidth) / static_cast<float>(g_WindowWidth) : 1.0f;
		if(m_PixelRatio <= 0.0f)
			m_PixelRatio = 1.0f;

		m_WindowSize = base::vec2(static_cast<float>(g_WindowWidth), static_cast<float>(g_WindowHeight));
	}

	void CGraphics::EndFrame(bool SwapBuffers)
	{
		if(SwapBuffers)
			glfwSwapBuffers(m_pWindow);
		m_Input.EndFrame();
	}

	void CGraphics::SetWindowTitle(const std::string &Title)
	{
		if(m_pWindow != nullptr)
			glfwSetWindowTitle(m_pWindow, Title.c_str());
	}

	int CGraphics::Texture(const std::string &Key) const
	{
		if(!m_Textures.contains(Key))
			return -1;
		return m_Textures.at(Key).value();
	}

	int CGraphics::LoadTextureFile(const std::string &Key, const storage::path &File)
	{
		if(m_Textures.contains(Key))
			return m_Textures.at(Key).value();
		const int Handle = nvgCreateImage(m_pContext, File.string().c_str(), NVG_IMAGE_NEAREST);
		if(Handle <= 0)
		{
			LOG_WARN("graphics", "failed to load the texture '%s'", File.string().c_str());
			m_Textures.emplace(Key, -1);
			return -1;
		}
		m_Textures.emplace(Key, Handle);
		LOG_TRACE("graphics", "loaded texture '%s' from '%s'", Key.c_str(), File.string().c_str());
		return Handle;
	}

	int CGraphics::CreateTextureRGBA(const std::string &Key, int Width, int Height, const storage::byte_vector &Pixels)
	{
		if(m_Textures.contains(Key))
			return m_Textures.at(Key).value();
		if(Width <= 0 || Height <= 0 || Pixels.empty())
			return -1;
		const int Handle = nvgCreateImageRGBA(m_pContext, Width, Height, NVG_IMAGE_NEAREST,
			reinterpret_cast<const unsigned char *>(Pixels.data()));
		if(Handle <= 0)
		{
			LOG_WARN("graphics", "failed to create the texture '%s'", Key.c_str());
			m_Textures.emplace(Key, -1);
			return -1;
		}
		m_Textures.emplace(Key, Handle);
		return Handle;
	}

	void CGraphics::ForgetTextures()
	{
		if(m_pContext == nullptr)
			return;
		for(auto &Entry : m_Textures)
		{
			const int Handle = Entry.value();
			if(Handle > 0)
				nvgDeleteImage(m_pContext, Handle);
		}
		m_Textures.clear();
	}

	float CGraphics::TextWidth(float Size, const std::string &Text, bool Bold) const
	{
		nvgFontFaceId(m_pContext, Bold ? m_FontBold : m_Font);
		nvgFontSize(m_pContext, Size);
		return nvgTextBounds(m_pContext, 0.0f, 0.0f, Text.c_str(), nullptr, nullptr);
	}
} // namespace engine
